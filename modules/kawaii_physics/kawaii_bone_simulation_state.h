// wgodot-changes::file
#pragma once

#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

class KawaiiBoneSimulator3D;

// A simulation checkpoint contains integration history, not rendered output.
// Pose evaluation and constraints still run normally after restoration.
class KawaiiBoneSimulationState : public RefCounted {
	GDCLASS(KawaiiBoneSimulationState, RefCounted);
	friend class KawaiiBoneSimulator3D;

	struct Particle {
		StringName bone;
		int parent = -1;
		Vector3 position;
		Vector3 previous_position;
	};
	struct Chain {
		StringName root;
		Transform3D previous_transform;
		double previous_delta = 0;
		Vector<Particle> particles;
	};
	Vector<Chain> chains;
	uint64_t random_seed = 0;
	uint64_t random_state = 0;
	bool has_random_state = false;

protected:
	static void _bind_methods();

public:
	Error add_chain(const StringName &p_root, const Transform3D &p_previous_transform, double p_previous_delta,
			const PackedStringArray &p_bones, const PackedInt32Array &p_parents,
			const PackedVector3Array &p_positions, const PackedVector3Array &p_previous_positions);
};
