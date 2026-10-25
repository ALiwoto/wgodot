// wgodot-changes::file
#include "scene_traversal.h"
#ifdef TOOLS_ENABLED
#include "render_capture.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "scene/animation/animation_player.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/main/viewport.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_environment.h"
#include "modules/particle_effects/particle_instance_3d.h"
#include "modules/particle_effects/particle_instance_pool_3d.h"
#endif

void WSceneTraversal::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin", "root", "roots", "output", "paths", "palettes", "world", "recursive"), &WSceneTraversal::begin, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("next", "deadline_usec"), &WSceneTraversal::next, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_completed"), &WSceneTraversal::is_completed);
}

void WSceneTraversal::begin(Node *p_root, const TypedArray<Node> &p_roots, const Ref<WSceneSnapshot> &p_output,
		const Ref<WCapturePaths> &p_paths, const Ref<ConfigFile> &p_palettes, bool p_world, bool p_recursive) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_root);
	ERR_FAIL_COND(p_output.is_null() || p_paths.is_null());
	root = p_root;
	viewport = root->get_viewport();
	output = p_output;
	paths = p_paths;
	palettes = p_palettes;
	world = p_world;
	recursive = p_recursive;
	pending.clear();
	for (int i = p_roots.size() - 1; i >= 0; i--) {
		Node *node = Object::cast_to<Node>(p_roots[i]);
		ERR_FAIL_NULL(node);
		pending.push_back(node);
	}
}

void WSceneTraversal::append_children(Node *p_node) {
	if (!recursive) {
		return;
	}
	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		pending.push_back(p_node->get_child(i));
	}
}

bool WSceneTraversal::capture(Node *p_node) {
	Viewport *node_viewport = Object::cast_to<Viewport>(p_node);
	if (Object::cast_to<CanvasItem>(p_node) || (node_viewport && node_viewport != viewport) || !output->begin_node(p_node)) {
		return false;
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	if (ParticleInstancePool3D *pool = Object::cast_to<ParticleInstancePool3D>(p_node)) {
		// Slots may be detached from the scene tree. Traverse acquired instances,
		// never the pool's free storage or its default particle birth identities.
		if (recursive) {
			TypedArray<ParticleInstance3D> instances = pool->get_instances(true);
			for (int i = instances.size() - 1; i >= 0; i--) {
				pending.push_back(Object::cast_to<ParticleInstance3D>(instances[i]));
			}
		}
		return false;
	}
#endif
	Node3D *spatial = Object::cast_to<Node3D>(p_node);
	String path;
	if (spatial && world) {
		path = paths->get_path(root, p_node);
		output->capture_spatial(spatial, path);
	}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	if (ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(p_node)) {
		if (!particle->is_visible()) {
			return false;
		}
		Node3D *target = particle->get_geometry();
		if (particle->get_decal()) {
			target = particle->get_decal();
		} else if (particle->get_light()) {
			target = particle->get_light();
		} else if (!target) {
			target = particle->get_pose();
		}
		ERR_FAIL_NULL_V_MSG(target, false, "Cannot capture a particle without bound geometry, decal, light or pose.");
		output->capture_particle(particle, paths->get_path(root, target));
		if (particle->get_light()) {
			output->capture_particle_light(particle->get_light(), paths->get_path(root, particle->get_light()));
		}
		append_children(p_node);
		return false;
	}
	if (ParticleGroundProjector3D *ground = Object::cast_to<ParticleGroundProjector3D>(p_node)) {
		output->capture_ground(ground, path.is_empty() ? paths->get_path(root, ground) : path);
		append_children(p_node);
		return false;
	}
#endif
	append_children(p_node);
	if (MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(p_node)) {
		if (mesh->get_mesh().is_null() || (!world && !WRenderCapture::render_visible(mesh))) {
			return false;
		}
		output->capture_mesh(mesh, path.is_empty() ? paths->get_path(root, mesh) : path, palettes);
		return true; // Material provenance and character classification belong to the project.
	}
	if (AnimationPlayer *animation = Object::cast_to<AnimationPlayer>(p_node)) {
		if (animation->is_playing()) {
			output->capture_animation(animation, "animation:" + paths->get_path(root, animation));
		}
		return false;
	}
	const StringName type = p_node->get_class_name();
	return p_node->get_script_instance() || (type != Node::get_class_static() && type != Node3D::get_class_static());
}

Node *WSceneTraversal::next(uint64_t p_deadline_usec) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), nullptr);
	ERR_FAIL_COND_V(output.is_null(), nullptr);
	while (!pending.is_empty()) {
		if (p_deadline_usec && OS::get_singleton()->get_ticks_usec() >= p_deadline_usec) {
			return nullptr;
		}
		Node *node = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		if (capture(node)) {
			return node;
		}
	}
	return nullptr;
}
#endif
