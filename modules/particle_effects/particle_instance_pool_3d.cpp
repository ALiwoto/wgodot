// wgodot-changes::file
#include "particle_instance_pool_3d.h"

#include "core/object/class_db.h"
#include "scene_preparation.h"
#include "scene/resources/3d/world_3d.h"

void ParticleInstancePool3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_visibility_aabb", "value"), &ParticleInstancePool3D::set_visibility_aabb);
	ClassDB::bind_method(D_METHOD("get_visibility_aabb"), &ParticleInstancePool3D::get_visibility_aabb);
	ADD_PROPERTY(PropertyInfo(Variant::AABB, "visibility_aabb"), "set_visibility_aabb", "get_visibility_aabb");
	ClassDB::bind_method(D_METHOD("prepare", "scene", "capacity"), &ParticleInstancePool3D::prepare);
	ClassDB::bind_method(D_METHOD("acquire"), &ParticleInstancePool3D::acquire);
	ClassDB::bind_method(D_METHOD("release", "instance"), &ParticleInstancePool3D::release);
	ClassDB::bind_method(D_METHOD("get_instances", "acquired_only"), &ParticleInstancePool3D::get_instances, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_instance_shader_parameter", "name", "value"), &ParticleInstancePool3D::set_instance_shader_parameter);
	ClassDB::bind_method(D_METHOD("get_capacity"), &ParticleInstancePool3D::get_capacity);
	ClassDB::bind_method(D_METHOD("get_available_count"), &ParticleInstancePool3D::get_available_count);
#ifdef TOOLS_ENABLED
	ClassDB::bind_method(D_METHOD("set_capture_lifecycle", "enabled"), &ParticleInstancePool3D::set_capture_lifecycle);
	ADD_SIGNAL(MethodInfo("instance_acquired", PropertyInfo(Variant::OBJECT, "instance", PROPERTY_HINT_RESOURCE_TYPE, "ParticleInstance3D")));
	ADD_SIGNAL(MethodInfo("instance_released", PropertyInfo(Variant::OBJECT, "instance", PROPERTY_HINT_RESOURCE_TYPE, "ParticleInstance3D")));
#endif
}

#ifdef TOOLS_ENABLED
void ParticleInstancePool3D::set_capture_lifecycle(bool p_enabled) {
	if (capture_lifecycle == p_enabled) {
		return;
	}
	capture_lifecycle = p_enabled;
	if (p_enabled) {
		for (int i = 0; i < instances.size(); i++) {
			if (acquired[i]) {
				emit_signal(SNAME("instance_acquired"), instances[i]);
			}
		}
	}
}
#endif

ParticleInstancePool3D::ParticleInstancePool3D() {
	set_notify_transform(true);
}

ParticleInstancePool3D::~ParticleInstancePool3D() {
	Vector<Node *> roots;
	roots.reserve(instances.size());
	for (ParticleInstance3D *instance : instances) {
		roots.push_back(instance);
	}
	ScenePreparation::free_branches_async(roots);
}

Error ParticleInstancePool3D::prepare(const Ref<PackedScene> &p_scene, int p_capacity) {
	ERR_FAIL_COND_V(is_inside_tree() || !instances.is_empty(), ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_scene.is_null() || p_capacity < 0, ERR_INVALID_PARAMETER);
	instances.reserve(p_capacity);
	available.reserve(p_capacity);
	acquired.resize(p_capacity);
	acquired.fill(0);
	for (int i = 0; i < p_capacity; ++i) {
		Node *node = p_scene->instantiate();
		ParticleInstance3D *instance = Object::cast_to<ParticleInstance3D>(node);
		if (!instance) {
			if (node) {
				memdelete(node);
			}
			return ERR_INVALID_DATA;
		}
		const Error error = ScenePreparation::prepare_branch(instance);
		if (error != OK) {
			memdelete(instance);
			return error;
		}
		instance->hide();
		instance->set_visibility_aabb(visibility_aabb);
		instance->prepare_render_instances(this);
		instances.push_back(instance);
		indices.insert(instance, i);
		available.push_back(i);
	}
	return OK;
}

void ParticleInstancePool3D::set_visibility_aabb(const AABB &p_value) {
	visibility_aabb = p_value;
	for (ParticleInstance3D *instance : instances) {
		instance->set_visibility_aabb(p_value);
	}
}

ParticleInstance3D *ParticleInstancePool3D::acquire() {
	ERR_FAIL_COND_V_MSG(available.is_empty(), nullptr, vformat("Prepared particle pool '%s' exhausted its %d instances. Check the emitter's lifetime and emission bounds before publishing the scene.", get_path(), instances.size()));
	const int index = available[available.size() - 1];
	available.resize(available.size() - 1);
	acquired.write[index] = 1;
#ifdef TOOLS_ENABLED
	if (capture_lifecycle) {
		emit_signal(SNAME("instance_acquired"), instances[index]);
	}
#endif
	return instances[index];
}

void ParticleInstancePool3D::release(ParticleInstance3D *p_instance) {
	const int *index = indices.getptr(p_instance);
	ERR_FAIL_NULL(index);
	ERR_FAIL_COND(!acquired[*index]);
	p_instance->hide();
	p_instance->publish_render_instances();
	acquired.write[*index] = 0;
	available.push_back(*index);
#ifdef TOOLS_ENABLED
	if (capture_lifecycle) {
		emit_signal(SNAME("instance_released"), p_instance);
	}
#endif
}

TypedArray<ParticleInstance3D> ParticleInstancePool3D::get_instances(bool p_acquired_only) const {
	TypedArray<ParticleInstance3D> result;
	result.resize(p_acquired_only ? instances.size() - available.size() : instances.size());
	int output = 0;
	for (int i = 0; i < instances.size(); ++i) {
		if (!p_acquired_only || acquired[i]) {
			result[output++] = instances[i];
		}
	}
	return result;
}

void ParticleInstancePool3D::set_instance_shader_parameter(const StringName &p_name, const Variant &p_value) {
	for (ParticleInstance3D *instance : instances) {
		instance->set_render_shader_parameter(p_name, p_value);
	}
}

void ParticleInstancePool3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			const RID scenario = get_world_3d()->get_scenario();
			for (ParticleInstance3D *instance : instances) {
				instance->set_render_scenario(scenario);
				instance->publish_render_instances();
			}
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			for (ParticleInstance3D *instance : instances) {
				instance->set_render_scenario(RID());
			}
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED: {
			for (int i = 0; i < instances.size(); ++i) {
				if (acquired[i]) {
					instances[i]->publish_render_instances();
				}
			}
		} break;
	}
}
