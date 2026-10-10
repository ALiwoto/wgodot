// wgodot-changes::file
#include "wgodot_motion_diagnostics.h"

#ifdef DEBUG_ENABLED

#include "godot_body_3d.h"

#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"
#include "scene/main/node.h"

namespace WGodotMotionDiagnostics {
thread_local Counters *current = nullptr;
static thread_local Motion *current_motion = nullptr;

struct BodyRecord {
	ObjectID object;
	uint64_t calls = 0;
	uint64_t stationary_calls = 0;
	uint64_t usec = 0;
	uint64_t maximum_usec = 0;
	Vector3 worst_position;
	Vector3 worst_motion;
	Counters phases[PHASE_COUNT];
};

struct ColliderRecord {
	ObjectID object;
	uint64_t calls = 0;
	uint64_t usec = 0;
	uint64_t triangles = 0;
	uint64_t gjk = 0;
};

struct Interval {
	uint64_t started = 0;
	HashMap<RID, BodyRecord> bodies;
	HashMap<RID, ColliderRecord> colliders;
};
static thread_local Interval interval;

struct MostExpensiveFirst {
	template <typename T>
	bool operator()(const T *a, const T *b) const {
		return a->usec > b->usec;
	}
};

void Counters::add(const Counters &p_other) {
	usec += p_other.usec;
	broadphase_candidates += p_other.broadphase_candidates;
	bvh_nodes += p_other.bvh_nodes;
	triangle_candidates += p_other.triangle_candidates;
	distance_calls += p_other.distance_calls;
	refinements += p_other.refinements;
	recovery_passes += p_other.recovery_passes;
	gjk_calls += p_other.gjk_calls;
	gjk_usec += p_other.gjk_usec;
	gjk_iterations += p_other.gjk_iterations;
	gjk_failures += p_other.gjk_failures;
	gjk_max_iterations = MAX(gjk_max_iterations, p_other.gjk_max_iterations);
	gjk_max_usec = MAX(gjk_max_usec, p_other.gjk_max_usec);
	for (int i = 0; i < 8; i++) {
		gjk_iteration_histogram[i] += p_other.gjk_iteration_histogram[i];
	}
}

static String object_label(ObjectID p_id) {
	Node *node = Object::cast_to<Node>(ObjectDB::get_instance(p_id));
	return node && node->is_inside_tree() ? String(node->get_path()) : vformat("object:%d", uint64_t(p_id));
}

static void report(uint64_t p_now) {
	uint64_t total_usec = 0;
	uint64_t calls = 0;
	Counters phases[PHASE_COUNT];
	Vector<const BodyRecord *> bodies;
	for (const KeyValue<RID, BodyRecord> &entry : interval.bodies) {
		bodies.push_back(&entry.value);
		total_usec += entry.value.usec;
		calls += entry.value.calls;
		for (int i = 0; i < PHASE_COUNT; i++) {
			phases[i].add(entry.value.phases[i]);
		}
	}
	bodies.sort_custom<MostExpensiveFirst>();
	print_line(vformat("WG motion interval wall_ms=%.3f queries=%d query_ms=%.3f bodies=%d (phase times overlap GJK times; verbose instrumentation included)",
			(p_now - interval.started) / 1000.0, calls, total_usec / 1000.0, bodies.size()));
	const char *names[PHASE_COUNT] = { "recovery", "sweep", "contact" };
	for (int i = 0; i < PHASE_COUNT; i++) {
		const Counters &c = phases[i];
		String histogram;
		for (int bin = 0; bin < 8; bin++) {
			histogram += (bin ? "," : "") + uitos(c.gjk_iteration_histogram[bin]);
		}
		print_line(vformat("WG motion phase=%s ms=%.3f broad_candidates=%d bvh_nodes=%d triangle_candidates=%d distance_calls=%d refinements=%d recovery_passes=%d",
				names[i], c.usec / 1000.0, c.broadphase_candidates, c.bvh_nodes, c.triangle_candidates, c.distance_calls, c.refinements, c.recovery_passes));
		print_line(vformat("WG motion gjk phase=%s calls=%d ms=%.3f iterations=%d max_iterations=%d failures=%d max_us=%d histogram_le_1_2_4_8_16_32_64_128=[%s]",
				names[i], c.gjk_calls, c.gjk_usec / 1000.0, c.gjk_iterations, c.gjk_max_iterations, c.gjk_failures, c.gjk_max_usec, histogram));
	}
	for (int i = 0; i < MIN(bodies.size(), 8); i++) {
		const BodyRecord &b = *bodies[i];
		print_line(vformat("WG motion body=%s calls=%d no_horizontal_motion=%d ms=%.3f worst_us=%d worst_position=%s worst_motion=%s phases_ms=%s",
				object_label(b.object), b.calls, b.stationary_calls, b.usec / 1000.0, b.maximum_usec, b.worst_position, b.worst_motion,
				Vector3(b.phases[0].usec / 1000.0, b.phases[1].usec / 1000.0, b.phases[2].usec / 1000.0)));
	}
	Vector<const ColliderRecord *> colliders;
	for (const KeyValue<RID, ColliderRecord> &entry : interval.colliders) {
		colliders.push_back(&entry.value);
	}
	colliders.sort_custom<MostExpensiveFirst>();
	for (int i = 0; i < MIN(colliders.size(), 8); i++) {
		const ColliderRecord &c = *colliders[i];
		print_line(vformat("WG motion collider=%s passes=%d ms=%.3f triangle_candidates=%d gjk_calls=%d",
				object_label(c.object), c.calls, c.usec / 1000.0, c.triangles, c.gjk));
	}
	interval.bodies.clear();
	interval.colliders.clear();
	interval.started = OS::get_singleton()->get_ticks_usec();
}

Motion::Motion(GodotBody3D *p_body, const Vector3 &p_position, const Vector3 &p_motion) {
	enabled = OS::get_singleton()->is_stdout_verbose();
	if (!enabled) {
		return;
	}
	body = p_body;
	position = p_position;
	motion = p_motion;
	previous_motion = current_motion;
	previous_counters = current;
	current_motion = this;
	current = nullptr;
	started = OS::get_singleton()->get_ticks_usec();
	if (!interval.started) {
		interval.started = started;
	}
}

Motion::~Motion() {
	if (!enabled) {
		return;
	}
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const uint64_t elapsed = now - started;
	BodyRecord &record = interval.bodies[body->get_self()];
	record.object = body->get_instance_id();
	record.calls++;
	record.stationary_calls += motion.x == 0 && motion.z == 0;
	record.usec += elapsed;
	if (elapsed > record.maximum_usec) {
		record.maximum_usec = elapsed;
		record.worst_position = position;
		record.worst_motion = motion;
	}
	for (int i = 0; i < PHASE_COUNT; i++) {
		record.phases[i].add(phases[i]);
	}
	current_motion = previous_motion;
	current = previous_counters;
	if (!current_motion && now - interval.started >= 2000000) {
		report(now);
	}
}

PhaseScope::PhaseScope(Phase p_phase) {
	previous = current;
	if (current_motion) {
		current = &current_motion->phases[p_phase];
		started = OS::get_singleton()->get_ticks_usec();
	}
}

PhaseScope::~PhaseScope() {
	if (started) {
		current->usec += OS::get_singleton()->get_ticks_usec() - started;
	}
	current = previous;
}

ColliderScope::ColliderScope(const GodotCollisionObject3D *p_collider) {
	if (current) {
		collider = p_collider;
		triangles_before = current->triangle_candidates;
		gjk_before = current->gjk_calls;
		started = OS::get_singleton()->get_ticks_usec();
	}
}

ColliderScope::~ColliderScope() {
	if (!collider) {
		return;
	}
	const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - started;
	ColliderRecord &record = interval.colliders[collider->get_self()];
	record.object = collider->get_instance_id();
	record.calls++;
	record.usec += elapsed;
	record.triangles += current->triangle_candidates - triangles_before;
	record.gjk += current->gjk_calls - gjk_before;
}

void record_gjk(uint64_t p_started, uint32_t p_iterations, bool p_failed) {
	const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - p_started;
	current->gjk_calls++;
	current->gjk_usec += elapsed;
	current->gjk_iterations += p_iterations;
	current->gjk_failures += p_failed;
	current->gjk_max_iterations = MAX(current->gjk_max_iterations, p_iterations);
	current->gjk_max_usec = MAX(current->gjk_max_usec, elapsed);
	int bin = 0;
	while (bin < 7 && p_iterations > uint32_t(1 << bin)) {
		bin++;
	}
	current->gjk_iteration_histogram[bin]++;
}
} // namespace WGodotMotionDiagnostics

#endif // DEBUG_ENABLED
