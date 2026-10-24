// wgodot-changes::file
#include "core/object/class_db.h"
#include "random_stream_32.h"

void RandomStream32::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset", "initial_state"), &RandomStream32::reset);
	ClassDB::bind_method(D_METHOD("get_state"), &RandomStream32::get_state);
	ClassDB::bind_method(D_METHOD("set_vector_order", "order"), &RandomStream32::set_vector_order);
	ClassDB::bind_method(D_METHOD("get_vector_order"), &RandomStream32::get_vector_order);
	ClassDB::bind_method(D_METHOD("next_float"), &RandomStream32::next_float);
	ClassDB::bind_method(D_METHOD("next_vector"), &RandomStream32::next_vector);
	ClassDB::bind_method(D_METHOD("next_weight", "axis_lock"), &RandomStream32::next_weight);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "state"), "reset", "get_state");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "vector_order"), "set_vector_order", "get_vector_order");
#ifdef TOOLS_ENABLED
	ClassDB::bind_method(D_METHOD("set_debug_observe_resets", "enabled"), &RandomStream32::set_debug_observe_resets);
	ADD_SIGNAL(MethodInfo("debug_reset", PropertyInfo(Variant::INT, "initial_state")));
#endif
}

#ifdef TOOLS_ENABLED
void RandomStream32::_debug_reset() {
	// Opt-in editor diagnostics run before the stream's first draw. A replay
	// observer may restore captured input state without recursively notifying.
	debug_notifying_reset = true;
	emit_signal(SNAME("debug_reset"), int64_t(state));
	debug_notifying_reset = false;
}
#endif

void RandomStream32::set_vector_order(const Vector3i &p_order) {
	ERR_FAIL_COND(p_order.x < 0 || p_order.x > 2 || p_order.y < 0 || p_order.y > 2 || p_order.z < 0 || p_order.z > 2);
	ERR_FAIL_COND(p_order.x == p_order.y || p_order.x == p_order.z || p_order.y == p_order.z);
	vector_order = p_order;
}

double RandomStream32::next_float() {
	state = state * 196314165U + 907633515U;
	return double(state & 0x7fffffU) / 8388608.0;
}

Vector3 RandomStream32::next_vector() {
	const real_t first = next_float();
	const real_t second = next_float();
	const real_t third = next_float();
	const real_t components[] = { first, second, third };
	return Vector3(components[vector_order.x], components[vector_order.y], components[vector_order.z]);
}

Vector3 RandomStream32::next_weight(int p_axis_lock) {
	Vector3 weight = next_vector();
	switch (p_axis_lock) {
		case 2:
			weight.y = weight.x;
			break;
		case 3:
			weight.z = weight.x;
			break;
		case 4:
			weight.y = weight.z;
			break;
		case 5:
			weight = Vector3(weight.x, weight.x, weight.x);
			break;
	}
	return weight;
}
