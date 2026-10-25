// wgodot-changes::file
#pragma once

#include "kawaii_bone_chain.h"
#include "kawaii_bone_simulation_state.h"

#include "core/math/random_pcg.h"
#include "core/templates/local_vector.h"
#include "scene/3d/skeleton_modifier_3d.h"

// Opt-in component-space solver. SpringBoneSimulator3D remains independent.
class KawaiiBoneSimulator3D : public SkeletonModifier3D {
	GDCLASS(KawaiiBoneSimulator3D, SkeletonModifier3D);

	struct Particle {
		int bone = -1;
		int parent = -1;
		int child_count = 0;
		Vector3 position;
		Vector3 previous_position;
		Transform3D pose;
		Transform3D output;
	};

	struct Collision {
		Ref<KawaiiBoneCollision> settings;
		int bone = -1;
		Vector3 center;
		Vector3 axis;
	};

	struct Chain {
		Ref<KawaiiBoneChain> settings;
		LocalVector<Particle> particles;
		LocalVector<Collision> collisions;
		real_t previous_delta = 1.0 / 60.0;
		Transform3D previous_transform;
		bool initialized = false;
	};

	TypedArray<KawaiiBoneChain> chains;
	LocalVector<Chain> simulation;
	LocalVector<Transform3D> poses;
	Vector3 wind_velocity;
	RandomPCG wind_random;
	bool dirty = true;
	bool teleport_detection_enabled = true;

	void _invalidate();
	void _build();
	void _add_bone(Chain &r_chain, int p_bone, int p_parent);
	void _snapshot_pose(Chain &r_chain);
	void _simulate(Chain &r_chain, const Transform3D &p_component, real_t p_delta);
	void _apply(Chain &r_chain);
	static void _collide(Particle &r_particle, const Collision &p_collision, real_t p_radius);

protected:
	static void _bind_methods();
	void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;
	void _validate_bone_names() override;
	void _set_active(bool p_active) override;
	void _process_modification(double p_delta) override;

public:
	void set_chains(const TypedArray<KawaiiBoneChain> &p_chains);
	TypedArray<KawaiiBoneChain> get_chains() const;
	void set_wind_velocity(const Vector3 &p_velocity);
	Vector3 get_wind_velocity() const;
	void set_teleport_detection_enabled(bool p_enabled);
	bool is_teleport_detection_enabled() const;
	void reset();
	Ref<KawaiiBoneSimulationState> capture_state();
	Error restore_state(const Ref<KawaiiBoneSimulationState> &p_state, const Transform3D &p_frame_change = Transform3D());
	bool has_process() const override { return true; }
	PackedStringArray get_configuration_warnings() const override;
};
