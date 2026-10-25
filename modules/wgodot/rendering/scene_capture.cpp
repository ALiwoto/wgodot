// wgodot-changes::file
#include "render_capture.h"
#include "scene_snapshot.h"
#include "capture_paths.h"

#ifdef TOOLS_ENABLED
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/mesh.h"
#include "core/object/callable_mp.h"
#include "core/os/semaphore.h"
#include "scene/resources/3d/skin.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/mesh_storage.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/main/viewport.h"
#include "scene/main/canvas_item.h"
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_3d.h"
#include "modules/particle_effects/particle_instance_pool_3d.h"
#endif

void WRenderCapture::PaletteBatch::collect(Node *p_node, Viewport *p_viewport) {
	Viewport *node_viewport = Object::cast_to<Viewport>(p_node);
	if (Object::cast_to<CanvasItem>(p_node) || (node_viewport && node_viewport != p_viewport)) {
		return;
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	ParticleInstancePool3D *pool = Object::cast_to<ParticleInstancePool3D>(p_node);
	if (pool) {
		TypedArray<ParticleInstance3D> particles = pool->get_instances(true);
		for (int i = 0; i < particles.size(); i++) {
			collect(Object::cast_to<ParticleInstance3D>(particles[i]), p_viewport);
		}
		return;
	}
#endif
	MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(p_node);
	if (mesh && mesh->get_skin().is_valid()) {
		Ref<SkinReference> skin = mesh->get_skin_reference();
		if (skin.is_valid()) {
			skins.insert(skin->get_skeleton());
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect(p_node->get_child(i), p_viewport);
	}
}

Ref<WRenderCapture::PaletteBatch> WRenderCapture::PaletteBatch::create(const TypedArray<Node> &p_roots) {
	Ref<PaletteBatch> batch;
	batch.instantiate();
	batch->result.instantiate();
	for (int i = 0; i < p_roots.size(); i++) {
		Node *root = Object::cast_to<Node>(p_roots[i]);
		ERR_FAIL_NULL_V(root, Ref<PaletteBatch>());
		batch->collect(root, root->get_viewport());
	}
	return batch;
}

void WRenderCapture::PaletteBatch::read(Ref<PaletteBatch> p_batch) {
	// The sampler holds its branches through frame submission. Only renderer
	// handles cross threads; SkinReference destruction belongs to the scene.
	for (const RID &skeleton : p_batch->skins) {
		TypedArray<Transform3D> poses;
		poses.resize(RSG::mesh_storage->skeleton_get_bone_count(skeleton));
		for (int bone = 0; bone < poses.size(); bone++) {
			poses[bone] = RSG::mesh_storage->skeleton_bone_get_transform(skeleton, bone);
		}
		p_batch->result->set_value(uitos(skeleton.get_id()), "poses", poses);
	}
	p_batch->completed.post();
}

Ref<ConfigFile> WRenderCapture::skeleton_palettes(const TypedArray<Node> &p_roots) {
	Ref<PaletteBatch> batch = PaletteBatch::create(p_roots);
	ERR_FAIL_COND_V(batch.is_null(), Ref<ConfigFile>());
	if (!batch->skins.is_empty()) {
		// Synchronous inspection outside an active recorded render uses the
		// same collector and reader. Playback queues it before its render.
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&PaletteBatch::read).bind(batch));
		batch->completed.wait();
	}
	return batch->result;
}

void WRenderCapture::queue_skeleton_palettes(const TypedArray<Node> &p_roots) {
	Ref<PaletteBatch> batch = PaletteBatch::create(p_roots);
	ERR_FAIL_COND(batch.is_null());
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp(this, &WRenderCapture::_queue_skeleton_palettes).bind(batch));
}

void WRenderCapture::_queue_skeleton_palettes(Ref<PaletteBatch> p_batch) {
	ERR_FAIL_COND_MSG(ending || completed.is_set(), "Cannot append skeleton palettes after a rendering capture has ended.");
	if (palette_request.is_valid()) {
		failure.set(ERR_BUSY);
		return;
	}
	palette_request = p_batch;
}

void WRenderCapture::_capture_skeleton_palettes() {
	Ref<ConfigFile> result;
	if (palette_request.is_valid()) {
		PaletteBatch::read(palette_request);
		result = palette_request->result;
		palette_request.unref();
	}
	MutexLock lock(palette_mutex);
	frame_palettes = result;
}

Ref<ConfigFile> WRenderCapture::get_skeleton_palettes() const {
	MutexLock lock(palette_mutex);
	return frame_palettes;
}

Transform3D WRenderCapture::render_transform(Node3D *p_node) {
	ERR_FAIL_NULL_V(p_node, Transform3D());
	if (p_node->is_inside_tree()) {
		return p_node->get_global_transform();
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	for (Node *parent = p_node; parent; parent = parent->get_parent()) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(parent);
		if (particle) {
			return particle->get_render_transform(p_node);
		}
	}
#endif
	ERR_FAIL_V_MSG(Transform3D(), "Detached capture node has no render owner.");
}

bool WRenderCapture::render_visible(Node3D *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	Viewport *viewport = nullptr;
	bool visible = false;
	if (p_node->is_inside_tree()) {
		visible = p_node->is_visible_in_tree();
		viewport = p_node->get_viewport();
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	else {
		for (Node *parent = p_node; parent; parent = parent->get_parent()) {
			ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(parent);
			if (particle) {
				visible = particle->is_render_visible(p_node);
				ParticleInstancePool3D *pool = particle->get_instance_pool();
				viewport = pool ? pool->get_viewport() : nullptr;
				break;
			}
		}
	}
#endif
	if (!visible) {
		return false;
	}
	const VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node);
	const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
	// RendererSceneCull applies this mask to geometry and shadow casters.
	// Keep nonvisual hierarchy nodes: their children have independent layers.
	return !visual || !camera || (visual->get_layer_mask() & camera->get_cull_mask()) != 0;
}

void WRenderCapture::write_skeleton(Skeleton3D *p_skeleton, const Ref<WSceneSnapshot> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_skeleton);
	ERR_FAIL_COND(p_output.is_null());
	p_output->capture_skeleton(p_skeleton, p_section);
}

void WRenderCapture::write_skeletons(Node *p_root, Node *p_node, const String &p_frame, const Ref<WSceneSnapshot> &p_output) {
	ERR_FAIL_NULL(p_root);
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null());
	// Traverse natively: no per-node/per-bone VM dispatch or temporary child
	// arrays. Stop at the same visibility and viewport boundaries as rendering.
	Viewport *viewport = Object::cast_to<Viewport>(p_node);
	if (Object::cast_to<CanvasItem>(p_node) || (viewport && viewport != p_root->get_viewport())) {
		return;
	}
	Node3D *spatial = Object::cast_to<Node3D>(p_node);
	if (spatial && !spatial->is_visible_in_tree()) {
		return;
	}
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
	if (skeleton) {
		write_skeleton(skeleton, p_output, p_frame + ":" + String(p_root->get_path_to(skeleton)));
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		write_skeletons(p_root, p_node->get_child(i), p_frame, p_output);
	}
}

void WRenderCapture::write_skeleton_list(Node *p_root, const TypedArray<Skeleton3D> &p_skeletons, const String &p_frame, const Ref<WSceneSnapshot> &p_output) {
	ERR_FAIL_NULL(p_root);
	ERR_FAIL_COND(p_output.is_null());
	for (int i = 0; i < p_skeletons.size(); i++) {
		Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_skeletons[i]);
		ERR_FAIL_NULL(skeleton);
		if (skeleton->get_viewport() != p_root->get_viewport()) {
			continue;
		}
		// Match the recursive recorder's visibility pruning, including hidden
		// ancestors separated from the rig by a plain Node (which breaks the
		// Node3D visibility-parent chain). Walk only each rig's ancestry.
		Node *ancestor = skeleton;
		for (; ancestor; ancestor = ancestor->get_parent()) {
			Node3D *spatial = Object::cast_to<Node3D>(ancestor);
			if (Object::cast_to<CanvasItem>(ancestor) || (spatial && !spatial->is_visible_in_tree())) {
				break;
			}
			if (ancestor == p_root) {
				write_skeleton(skeleton, p_output, p_frame + ":" + String(p_root->get_path_to(skeleton)));
				break;
			}
		}
	}
}

void WRenderCapture::write_spatial(Node3D *p_node, const Ref<WSceneSnapshot> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null());
	p_output->capture_spatial(p_node, p_section);
}

void WRenderCapture::write_mesh(MeshInstance3D *p_node, const Ref<WSceneSnapshot> &p_output, const String &p_section, const Ref<ConfigFile> &p_palettes) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null() || p_node->get_mesh().is_null());
	p_output->capture_mesh(p_node, p_section, p_palettes);
}

void WRenderCapture::write_transforms(const TypedArray<Node3D> &p_nodes, const Ref<WSceneSnapshot> &p_output, const String &p_section) {
	ERR_FAIL_COND(p_output.is_null());
	p_output->capture_transforms(p_nodes, p_section);
}

#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
void WRenderCapture::write_particle_transforms(const TypedArray<ParticleInstance3D> &p_particles, const Ref<WSceneSnapshot> &p_output, const String &p_section) {
	ERR_FAIL_COND(p_output.is_null());
	p_output->capture_particle_transforms(p_particles, p_section);
}

void WRenderCapture::write_particle(ParticleInstance3D *p_particle, const Ref<WSceneSnapshot> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_particle);
	ERR_FAIL_COND(p_output.is_null());
	p_output->capture_particle(p_particle, p_section);
}
#endif
#endif
