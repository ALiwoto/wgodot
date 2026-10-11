// wgodot-changes::file
// Adapted from KawaiiPhysics v1.7.0, Copyright (c) 2019-2020 pafuhana1213.
// MIT license: see LICENSE in this directory.
// https://github.com/pafuhana1213/KawaiiPhysics/tree/20201202-v1.7.0
#include "kawaii_bone_simulator_3d.h"

#include "core/math/geometry_3d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

// Unreal's GetSafeNormal threshold converted from centimeters to meters.
static Vector3 kawaii_direction(const Vector3 &p_vector) {
	const real_t squared = p_vector.length_squared();
	return squared > 1e-12 ? p_vector / Math::sqrt(squared) : Vector3();
}

static Quaternion kawaii_rotation(const Vector3 &p_from, const Vector3 &p_to) {
	const real_t norm = Math::sqrt(p_from.length_squared() * p_to.length_squared());
	const real_t scalar = norm + p_from.dot(p_to);
	Quaternion rotation;
	if (scalar >= norm * 1e-6) {
		const Vector3 axis = p_from.cross(p_to);
		rotation = Quaternion(axis.x, axis.y, axis.z, scalar);
	} else if (Math::abs(p_from.x) > Math::abs(p_from.z)) {
		// Y-up equivalent of the original Z-up opposite-vector fallback.
		rotation = Quaternion(p_from.y, -p_from.x, 0, 0);
	} else {
		rotation = Quaternion(0, -p_from.z, p_from.y, 0);
	}
	// Unlike Godot's shortest-arc constructor, Kawaii keeps tiny rotations.
	return rotation.normalized();
}

void KawaiiBoneSimulator3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_chains", "chains"), &KawaiiBoneSimulator3D::set_chains);
	ClassDB::bind_method(D_METHOD("get_chains"), &KawaiiBoneSimulator3D::get_chains);
	ClassDB::bind_method(D_METHOD("set_wind_velocity", "velocity"), &KawaiiBoneSimulator3D::set_wind_velocity);
	ClassDB::bind_method(D_METHOD("get_wind_velocity"), &KawaiiBoneSimulator3D::get_wind_velocity);
	ClassDB::bind_method(D_METHOD("reset"), &KawaiiBoneSimulator3D::reset);
	ClassDB::bind_method(D_METHOD("set_teleport_detection_enabled", "enabled"), &KawaiiBoneSimulator3D::set_teleport_detection_enabled);
	ClassDB::bind_method(D_METHOD("is_teleport_detection_enabled"), &KawaiiBoneSimulator3D::is_teleport_detection_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "chains", PROPERTY_HINT_ARRAY_TYPE, "KawaiiBoneChain"), "set_chains", "get_chains");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind_velocity", "get_wind_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "teleport_detection_enabled"), "set_teleport_detection_enabled", "is_teleport_detection_enabled");
}

void KawaiiBoneSimulator3D::set_chains(const TypedArray<KawaiiBoneChain> &p_chains) {
	const Callable changed = callable_mp(this, &KawaiiBoneSimulator3D::_invalidate);
	for (int i = 0; i < chains.size(); i++) {
		Ref<KawaiiBoneChain> chain = chains[i];
		if (chain.is_valid() && chain->is_connected(SNAME("changed"), changed)) {
			chain->disconnect_changed(changed);
		}
	}
	chains = p_chains.duplicate();
	for (int i = 0; i < chains.size(); i++) {
		Ref<KawaiiBoneChain> chain = chains[i];
		if (chain.is_valid() && !chain->is_connected(SNAME("changed"), changed)) {
			chain->connect_changed(changed);
		}
	}
	_invalidate();
}

TypedArray<KawaiiBoneChain> KawaiiBoneSimulator3D::get_chains() const {
	return chains.duplicate();
}

void KawaiiBoneSimulator3D::set_wind_velocity(const Vector3 &p_velocity) {
	wind_velocity = p_velocity;
}

Vector3 KawaiiBoneSimulator3D::get_wind_velocity() const {
	return wind_velocity;
}

void KawaiiBoneSimulator3D::set_teleport_detection_enabled(bool p_enabled) {
	teleport_detection_enabled = p_enabled;
}

bool KawaiiBoneSimulator3D::is_teleport_detection_enabled() const {
	return teleport_detection_enabled;
}

void KawaiiBoneSimulator3D::reset() {
	initialized = false;
}

void KawaiiBoneSimulator3D::_invalidate() {
	dirty = true;
	reset();
	update_configuration_warnings();
}

void KawaiiBoneSimulator3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	const Callable changed = callable_mp(this, &KawaiiBoneSimulator3D::_invalidate);
	for (const StringName signal : { SNAME("bone_list_changed"), SNAME("rest_updated") }) {
		if (p_old && p_old->is_connected(signal, changed)) {
			p_old->disconnect(signal, changed);
		}
		if (p_new && !p_new->is_connected(signal, changed)) {
			p_new->connect(signal, changed);
		}
	}
	_invalidate();
}

void KawaiiBoneSimulator3D::_validate_bone_names() {
	_invalidate();
}

void KawaiiBoneSimulator3D::_set_active(bool p_active) {
	reset();
}

PackedStringArray KawaiiBoneSimulator3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return warnings;
	}
	for (int i = 0; i < chains.size(); i++) {
		Ref<KawaiiBoneChain> chain = chains[i];
		if (chain.is_null()) {
			warnings.push_back(vformat("Hair chain %d has no settings.", i));
			continue;
		}
		if (skeleton->find_bone(chain->get_root_bone()) < 0) {
			warnings.push_back(vformat("Chain root '%s' is not in this skeleton.", chain->get_root_bone()));
		}
		TypedArray<KawaiiBoneCollision> collisions = chain->get_collisions();
		for (int j = 0; j < collisions.size(); j++) {
			Ref<KawaiiBoneCollision> collision = collisions[j];
			if (collision.is_valid() && !collision->get_bone_name().is_empty() && skeleton->find_bone(collision->get_bone_name()) < 0) {
				warnings.push_back(vformat("Collision bone '%s' is not in this skeleton.", collision->get_bone_name()));
			}
		}
	}
	return warnings;
}

void KawaiiBoneSimulator3D::_add_bone(Chain &r_chain, int p_bone, int p_parent) {
	Skeleton3D *skeleton = get_skeleton();
	if (r_chain.settings->get_excluded_bones().has(skeleton->get_bone_name(p_bone))) {
		return;
	}
	Particle particle;
	particle.bone = p_bone;
	particle.parent = p_parent;
	const int index = r_chain.particles.size();
	r_chain.particles.push_back(particle);
	if (p_parent >= 0) {
		r_chain.particles[p_parent].child_count++;
	}
	const Vector<int> children = skeleton->get_bone_children(p_bone);
	for (int child : children) {
		_add_bone(r_chain, child, index);
	}
	if (children.is_empty() && r_chain.settings->get_dummy_length() > 0.0) {
		Particle tip;
		tip.parent = index;
		r_chain.particles.push_back(tip);
		r_chain.particles[index].child_count++;
	}
}

void KawaiiBoneSimulator3D::_build() {
	simulation.clear();
	Skeleton3D *skeleton = get_skeleton();
	for (int i = 0; i < chains.size(); i++) {
		Ref<KawaiiBoneChain> settings = chains[i];
		if (settings.is_null()) {
			continue;
		}
		const int root = skeleton->find_bone(settings->get_root_bone());
		if (root < 0) {
			ERR_PRINT(vformat("Kawaii chain root '%s' is not in this skeleton.", settings->get_root_bone()));
			continue;
		}
		Chain chain;
		chain.settings = settings;
		_add_bone(chain, root, -1);
		const TypedArray<KawaiiBoneCollision> collisions = settings->get_collisions();
		// Kawaii resolves spheres, then capsules, then planes, regardless of authoring order.
		for (int shape = KawaiiBoneCollision::SHAPE_SPHERE; shape <= KawaiiBoneCollision::SHAPE_PLANE; shape++) {
			for (int j = 0; j < collisions.size(); j++) {
				Ref<KawaiiBoneCollision> collision = collisions[j];
				if (collision.is_null() || collision->get_shape() != shape) {
					continue;
				}
				Collision cached;
				cached.settings = collision;
				cached.bone = skeleton->find_bone(collision->get_bone_name());
				chain.collisions.push_back(cached);
			}
		}
		simulation.push_back(chain);
	}
	dirty = false;
}

void KawaiiBoneSimulator3D::_snapshot_pose(Chain &r_chain) {
	for (Particle &particle : r_chain.particles) {
		if (particle.bone >= 0) {
			particle.pose = poses[particle.bone];
		} else {
			particle.pose = r_chain.particles[particle.parent].pose;
			particle.pose.origin += particle.pose.basis.get_rotation_quaternion().xform(r_chain.settings->get_forward_axis()) * r_chain.settings->get_dummy_length();
		}
		particle.output = particle.pose;
		if (!initialized) {
			particle.position = particle.pose.origin;
			particle.previous_position = particle.position;
		}
	}
	for (Collision &collision : r_chain.collisions) {
		const Transform3D offset = collision.settings->get_offset();
		collision.center = offset.origin;
		collision.axis = offset.basis.get_rotation_quaternion().xform(Vector3(0, 1, 0));
		if (collision.bone >= 0) {
			const Transform3D &pose = poses[collision.bone];
			collision.center = pose.xform(collision.center);
			collision.axis = pose.basis.get_rotation_quaternion().xform(collision.axis);
		}
	}
	if (!initialized) {
		r_chain.previous_delta = 1.0 / r_chain.settings->get_target_framerate();
	}
}

void KawaiiBoneSimulator3D::_collide(Particle &r_particle, const Collision &p_collision, real_t p_radius) {
	const KawaiiBoneCollision &settings = **p_collision.settings;
	const real_t radius = settings.get_radius();
	const real_t distance = radius + p_radius;
	const Vector3 offset = r_particle.position - p_collision.center;
	switch (settings.get_shape()) {
		case KawaiiBoneCollision::SHAPE_SPHERE: {
			if (radius <= 0.0) {
				return;
			}
			if (settings.get_inside()) {
				if (offset.length_squared() >= distance * distance) {
					r_particle.position = p_collision.center + kawaii_direction(offset) * (radius - p_radius);
				}
			} else if (offset.length_squared() <= distance * distance) {
				r_particle.position += kawaii_direction(offset) * (distance - offset.length());
			}
		} break;
		case KawaiiBoneCollision::SHAPE_CAPSULE: {
			// Zero radius/length disables the authored shape; it is not a sphere fallback.
			if (radius <= 0.0 || settings.get_length() <= 0.0) {
				return;
			}
			const Vector3 half_axis = p_collision.axis * (settings.get_length() * 0.5);
			const Vector3 closest = Geometry3D::get_closest_point_to_segment(r_particle.position, p_collision.center - half_axis, p_collision.center + half_axis);
			const Vector3 separation = r_particle.position - closest;
			if (separation.length_squared() < distance * distance) {
				r_particle.position = closest + kawaii_direction(separation) * distance;
			}
		} break;
		case KawaiiBoneCollision::SHAPE_PLANE: {
			const real_t current_side = p_collision.axis.dot(offset);
			const real_t previous_side = p_collision.axis.dot(r_particle.previous_position - p_collision.center);
			const real_t travel = previous_side - current_side;
			const real_t intersection = travel != 0.0 ? -current_side / travel : -1.0;
			const bool crossing = intersection > -0.0001 && intersection < 1.0001;
			// A swept surface, not Godot's infinite solid half-space.
			if (current_side * current_side < p_radius * p_radius || crossing) {
				r_particle.position += p_collision.axis * (p_radius - current_side);
			}
		} break;
	}
}

void KawaiiBoneSimulator3D::_simulate(Chain &r_chain, const Transform3D &p_component, real_t p_delta) {
	const KawaiiBoneChain &settings = **r_chain.settings;
	const Transform3D inverse_component = p_component.affine_inverse();
	Vector3 translation = inverse_component.xform(previous_transform.origin);
	if (teleport_detection_enabled && translation.length_squared() > settings.get_teleport_distance() * settings.get_teleport_distance()) {
		translation = Vector3();
	}
	Quaternion rotation = p_component.basis.get_rotation_quaternion().inverse() * previous_transform.basis.get_rotation_quaternion();
	// Matrix-to-quaternion conversion can flip q to -q during a full turn.
	// That representation change must not count as a near-360-degree teleport.
	if (rotation.w < 0.0) {
		rotation = -rotation;
	}
	if (teleport_detection_enabled && settings.get_teleport_rotation() >= 0.0 && Math::rad_to_deg(rotation.get_angle()) > settings.get_teleport_rotation()) {
		rotation = Quaternion();
	}
	const real_t stiffness_weight = 1.0 - Math::pow(real_t(1.0) - settings.get_stiffness(), settings.get_target_framerate() * p_delta);
	const Vector3 gravity = inverse_component.basis.xform(settings.get_gravity()) * (0.5 * p_delta * p_delta);
	const Vector3 wind = inverse_component.basis.xform(wind_velocity) * (settings.get_wind_scale() * settings.get_target_framerate());
	for (Particle &particle : r_chain.particles) {
		if (particle.parent < 0) {
			particle.previous_position = particle.position;
			particle.position = particle.pose.origin;
			continue;
		}
		Particle &parent = r_chain.particles[particle.parent];
		const Vector3 pose_direction = particle.pose.origin - parent.pose.origin;
		Vector3 velocity = (particle.position - particle.previous_position) / r_chain.previous_delta;
		particle.previous_position = particle.position;
		velocity *= 1.0 - settings.get_damping();
		if (settings.get_wind_enabled() && !wind.is_zero_approx()) {
			velocity += wind * (wind_random.randf() * 2.0);
		}
		particle.position += velocity * p_delta;
		particle.position += translation * (1.0 - settings.get_world_damping_translation());
		particle.position += (rotation.xform(particle.previous_position) - particle.previous_position) * (1.0 - settings.get_world_damping_rotation());
		particle.position += gravity;
		particle.position += (parent.position + pose_direction - particle.position) * stiffness_weight;
		for (const Collision &collision : r_chain.collisions) {
			_collide(particle, collision, settings.get_radius());
		}
		if (settings.get_limit_angle() > 0.0) {
			const Vector3 direction = particle.position - parent.position;
			const Vector3 pose_normal = kawaii_direction(pose_direction);
			const Vector3 normal = kawaii_direction(direction);
			const Vector3 axis = pose_normal.cross(normal);
			const real_t excess = Math::atan2(axis.length(), pose_normal.dot(normal)) - Math::deg_to_rad(settings.get_limit_angle());
			if (excess > 0.0) {
				// The shipped Kawaii implementation passes the raw cross product
				// to RotateAngleAxis. Preserve that response, including its scale,
				// before the final length restoration below.
				const real_t cosine = Math::cos(-excess);
				const Vector3 adjusted = normal * cosine + axis.cross(normal) * Math::sin(-excess) + axis * axis.dot(normal) * (1.0 - cosine);
				particle.position = parent.position + adjusted * direction.length();
			}
		}
		if (settings.get_planar_constraint() != KawaiiBoneChain::PLANAR_CONSTRAINT_NONE) {
			Vector3 axis;
			axis[settings.get_planar_constraint() - 1] = 1.0;
			const Vector3 normal = parent.pose.basis.get_rotation_quaternion().xform(axis);
			particle.position -= normal * normal.dot(particle.position - parent.position);
		}
		// Restore the animated segment length once, after every constraint.
		particle.position = parent.position + kawaii_direction(particle.position - parent.position) * pose_direction.length();
	}
	r_chain.previous_delta = p_delta;
}

void KawaiiBoneSimulator3D::_apply(Chain &r_chain) {
	for (Particle &particle : r_chain.particles) {
		if (particle.parent < 0) {
			continue;
		}
		Particle &parent = r_chain.particles[particle.parent];
		if (parent.child_count == 1) {
			const Vector3 from = particle.pose.origin - parent.pose.origin;
			const Vector3 to = particle.position - parent.position;
			if (from.length_squared() > 1e-12 && to.length_squared() > 1e-12) {
				parent.output.basis = Basis(kawaii_rotation(from, to)) * parent.pose.basis;
			}
		}
		particle.output.origin = particle.position;
	}
	Skeleton3D *skeleton = get_skeleton();
	for (const Particle &particle : r_chain.particles) {
		if (particle.bone >= 0) {
			skeleton->set_bone_global_pose(particle.bone, particle.output);
		}
	}
}

void KawaiiBoneSimulator3D::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton || p_delta <= 0.0) {
		return;
	}
	if (dirty) {
		_build();
	}
	// Snapshot before writing any bone: a simulated parent must not change the
	// animation pose used by stiffness, collision placement, or length constraints.
	poses.resize(skeleton->get_bone_count());
	for (uint32_t i = 0; i < poses.size(); i++) {
		poses[i] = skeleton->get_bone_global_pose(i);
	}
	const Transform3D component = skeleton->get_global_transform();
	if (!initialized) {
		previous_transform = component;
	}
	for (Chain &chain : simulation) {
		_snapshot_pose(chain);
		_simulate(chain, component, p_delta);
		_apply(chain);
	}
	previous_transform = component;
	initialized = true;
}
