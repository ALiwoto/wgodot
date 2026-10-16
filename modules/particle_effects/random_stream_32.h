// wgodot-changes::file
#pragma once

#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/object/ref_counted.h"

// A reproducible 32-bit LCG. Each simulation owns its streams; sampling never
// touches the global RNG. Axis order is explicit when converting authored data.
class RandomStream32 : public RefCounted {
	GDCLASS(RandomStream32, RefCounted);

	uint32_t state = 0;
	Vector3i vector_order = Vector3i(0, 1, 2);

protected:
	static void _bind_methods();

public:
	void reset(int64_t p_state) { state = uint32_t(p_state); }
	int64_t get_state() const { return state; }
	void set_vector_order(const Vector3i &p_order);
	Vector3i get_vector_order() const { return vector_order; }
	double next_float();
	Vector3 next_vector();
	Vector3 next_weight(int p_axis_lock);
};
