// wgodot-changes::file
#pragma once

#ifdef DEBUG_ENABLED

#include "core/math/vector3.h"
#include "core/object/object_id.h"
#include "core/templates/rid.h"

class GodotBody3D;
class GodotCollisionObject3D;

// Debug-only, opt-in, per-thread counters for CharacterBody motion queries. No per-face
// logging, allocations or clock reads when --verbose is absent.
namespace WGodotMotionDiagnostics {
enum Phase {
	RECOVERY,
	SWEEP,
	CONTACT,
	PHASE_COUNT,
};

struct Counters {
	uint64_t usec = 0;
	uint64_t broadphase_candidates = 0;
	uint64_t bvh_nodes = 0;
	uint64_t triangle_candidates = 0;
	uint64_t distance_calls = 0;
	uint64_t refinements = 0;
	uint64_t recovery_passes = 0;
	uint64_t gjk_calls = 0;
	uint64_t gjk_usec = 0;
	uint64_t gjk_iterations = 0;
	uint64_t gjk_failures = 0;
	uint64_t gjk_max_iterations = 0;
	uint64_t gjk_max_usec = 0;
	uint64_t gjk_iteration_histogram[8] = {};

	void add(const Counters &p_other);
};

extern thread_local Counters *current;

class Motion {
	friend class PhaseScope;
	bool enabled = false;
	Motion *previous_motion = nullptr;
	Counters *previous_counters = nullptr;
	uint64_t started = 0;
	GodotBody3D *body = nullptr;
	Vector3 position;
	Vector3 motion;
	Counters phases[PHASE_COUNT];

public:
	Motion(GodotBody3D *p_body, const Vector3 &p_position, const Vector3 &p_motion);
	~Motion();
};

class PhaseScope {
	Counters *previous = nullptr;
	uint64_t started = 0;

public:
	PhaseScope(Phase p_phase);
	~PhaseScope();
};

class ColliderScope {
	const GodotCollisionObject3D *collider = nullptr;
	uint64_t started = 0;
	uint64_t triangles_before = 0;
	uint64_t gjk_before = 0;

public:
	ColliderScope(const GodotCollisionObject3D *p_collider);
	~ColliderScope();
};

void record_gjk(uint64_t p_started, uint32_t p_iterations, bool p_failed);
} // namespace WGodotMotionDiagnostics

#endif // DEBUG_ENABLED
