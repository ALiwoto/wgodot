// wgodot-changes::file
#include "render_capture.h"

#ifdef TOOLS_ENABLED
#include "scene/3d/mesh_instance_3d.h"
#include "core/object/callable_mp.h"
#include "core/os/semaphore.h"
#include "scene/resources/3d/skin.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/mesh_storage.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/main/viewport.h"
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_3d.h"
#include "modules/particle_effects/particle_instance_pool_3d.h"
#endif

void WRenderCapture::PaletteBatch::collect(Node *p_node, Viewport *p_viewport) {
	Viewport *node_viewport = Object::cast_to<Viewport>(p_node);
	if (node_viewport && node_viewport != p_viewport) {
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
	if (p_node->is_inside_tree()) {
		return p_node->is_visible_in_tree();
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	for (Node *parent = p_node; parent; parent = parent->get_parent()) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(parent);
		if (particle) {
			return particle->is_render_visible(p_node);
		}
	}
#endif
	return false;
}

void WRenderCapture::write_skeleton(Skeleton3D *p_skeleton, const Ref<ConfigFile> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_skeleton);
	ERR_FAIL_COND(p_output.is_null());
	const int count = p_skeleton->get_bone_count();
	PackedStringArray names;
	names.resize(count);
	TypedArray<Transform3D> poses;
	poses.resize(count);
	for (int bone = 0; bone < count; bone++) {
		names.set(bone, p_skeleton->get_bone_name(bone));
		poses[bone] = p_skeleton->get_bone_global_pose(bone);
	}
	p_output->set_value(p_section, "bones", names);
	p_output->set_value(p_section, "poses", poses);
}

void WRenderCapture::write_skeletons(Node *p_root, Node *p_node, const String &p_frame, const Ref<ConfigFile> &p_output) {
	ERR_FAIL_NULL(p_root);
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null());
	// Traverse natively: no per-node/per-bone VM dispatch or temporary child
	// arrays. Stop at the same visibility and viewport boundaries as rendering.
	Viewport *viewport = Object::cast_to<Viewport>(p_node);
	if (viewport && viewport != p_root->get_viewport()) {
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

void WRenderCapture::write_spatial(Node3D *p_node, const Ref<ConfigFile> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null());
	p_output->set_value(p_section, "class", p_node->get_class());
	p_output->set_value(p_section, "instance_id", int64_t(p_node->get_instance_id()));
	p_output->set_value(p_section, "transform", render_transform(p_node));
	p_output->set_value(p_section, "visible", render_visible(p_node));
	if (!p_node->get_scene_file_path().is_empty() || !p_output->has_section_key(p_section, "source_scene")) {
		p_output->set_value(p_section, "source_scene", p_node->get_scene_file_path());
	}
}

void WRenderCapture::write_mesh(MeshInstance3D *p_node, const Ref<ConfigFile> &p_output, const String &p_section, const Ref<ConfigFile> &p_palettes) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_output.is_null() || p_node->get_mesh().is_null());
	p_output->set_value(p_section, "visible", render_visible(p_node));
	p_output->set_value(p_section, "transform", render_transform(p_node));
	p_output->set_value(p_section, "layers", p_node->get_layer_mask());
	p_output->set_value(p_section, "mesh", p_node->get_mesh()->get_path());
	p_output->set_value(p_section, "mesh_rid", int64_t(p_node->get_mesh()->get_rid().get_id()));
	auto shadow = p_node->get_cast_shadows_setting();
	p_output->set_value(p_section, "cast_shadows", shadow != GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	p_output->set_value(p_section, "two_sided_shadow", shadow == GeometryInstance3D::SHADOW_CASTING_SETTING_DOUBLE_SIDED);
	p_output->set_value(p_section, "shadow_mode", int(shadow));
	p_output->set_value(p_section, "transparency", p_node->get_transparency());
	p_output->set_value(p_section, "visibility_begin", p_node->get_visibility_range_begin());
	p_output->set_value(p_section, "visibility_end", p_node->get_visibility_range_end());
	p_output->set_value(p_section, "extra_cull_margin", p_node->get_extra_cull_margin());
	p_output->set_value(p_section, "custom_aabb", p_node->get_custom_aabb());
	instance_uniforms(p_node, p_output, p_section, "instance/");
	Ref<Skin> skin = p_node->get_skin();
	if (skin.is_valid()) {
		Ref<SkinReference> reference = p_node->get_skin_reference();
		if (reference.is_valid()) {
			String key = uitos(reference->get_skeleton().get_id());
			ERR_FAIL_COND_MSG(p_palettes.is_null() || !p_palettes->has_section_key(key, "poses"), "Skin palette missing from frozen snapshot.");
			TypedArray<Transform3D> palette = p_palettes->get_value(key, "poses");
			ERR_FAIL_COND_MSG(palette.size() != skin->get_bind_count(), "Rendered skin palette and snapshot bindings differ.");
			p_output->set_value(p_section, "skin/palette", palette);
		}
		const int count = skin->get_bind_count();
		PackedStringArray names;
		PackedInt32Array bones;
		TypedArray<Transform3D> poses;
		names.resize(count);
		bones.resize(count);
		poses.resize(count);
		for (int i = 0; i < count; i++) {
			names.set(i, skin->get_bind_name(i));
			bones.set(i, skin->get_bind_bone(i));
			poses[i] = skin->get_bind_pose(i);
		}
		p_output->set_value(p_section, "skin/names", names);
		p_output->set_value(p_section, "skin/bones", bones);
		p_output->set_value(p_section, "skin/poses", poses);
	}
}

void WRenderCapture::write_transforms(const TypedArray<Node3D> &p_nodes, const Ref<ConfigFile> &p_output, const String &p_section) {
	ERR_FAIL_COND(p_output.is_null());
	const int count = p_nodes.size();
	PackedInt64Array identities;
	TypedArray<Transform3D> transforms;
	PackedByteArray visible;
	identities.resize(count);
	transforms.resize(count);
	visible.resize(count);
	for (int i = 0; i < count; i++) {
		Node3D *node = Object::cast_to<Node3D>(p_nodes[i]);
		ERR_FAIL_NULL(node);
		identities.set(i, int64_t(node->get_instance_id()));
		transforms[i] = node->get_global_transform_interpolated();
		visible.set(i, node->is_visible_in_tree());
	}
	p_output->set_value(p_section, "identities", identities);
	p_output->set_value(p_section, "transforms", transforms);
	p_output->set_value(p_section, "visible", visible);
}

#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
void WRenderCapture::write_particle_transforms(const TypedArray<ParticleInstance3D> &p_particles, const Ref<ConfigFile> &p_output, const String &p_section) {
	ERR_FAIL_COND(p_output.is_null());
	const int count = p_particles.size();
	PackedInt64Array identities;
	PackedInt32Array births;
	TypedArray<Transform3D> transforms;
	PackedByteArray visible;
	identities.resize(count);
	births.resize(count);
	transforms.resize(count);
	visible.resize(count);
	for (int i = 0; i < count; i++) {
		ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(p_particles[i]);
		ERR_FAIL_NULL(particle);
		identities.set(i, int64_t(particle->get_instance_id()));
		births.set(i, particle->get_spawn_index());
		transforms[i] = particle->get_render_transform(particle);
		visible.set(i, particle->is_render_visible(particle));
	}
	p_output->set_value(p_section, "particle_identities", identities);
	p_output->set_value(p_section, "particle_births", births);
	p_output->set_value(p_section, "particle_transforms", transforms);
	p_output->set_value(p_section, "particle_visible", visible);
}

void WRenderCapture::write_particle(ParticleInstance3D *p_particle, const Ref<ConfigFile> &p_output, const String &p_section) {
	ERR_FAIL_NULL(p_particle);
	ERR_FAIL_COND(p_output.is_null());
	Color color = p_particle->get_particle_render_color();
	p_output->set_value(p_section, "particle/age", p_particle->get_particle_previous_age());
	p_output->set_value(p_section, "particle/color", Vector4(color.r, color.g, color.b, color.a));
	p_output->set_value(p_section, "particle/lifetime", p_particle->get_particle_lifetime());
	if (p_particle->get_sprite_motion().is_valid()) {
		p_output->set_value(p_section, "particle/relative_age", p_particle->get_particle_relative_age());
		p_output->set_value(p_section, "particle/transform", p_particle->get_particle_transform());
		p_output->set_value(p_section, "particle/position", p_particle->get_particle_position());
		p_output->set_value(p_section, "particle/velocity", p_particle->get_particle_velocity());
		p_output->set_value(p_section, "particle/base_size", p_particle->get_particle_base_size());
		p_output->set_value(p_section, "particle/angle", p_particle->get_particle_angle());
	}
	p_output->set_value(p_section, "particle/orbit_position", p_particle->get_particle_render_orbit_offset());
	p_output->set_value(p_section, "particle/orbit_offset", p_particle->get_particle_orbit_offset());
	p_output->set_value(p_section, "particle/orbit_rotation", p_particle->get_particle_orbit_rotation());
}
#endif
#endif
