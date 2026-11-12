// wgodot-changes::file
#pragma once

#include "core/object/object.h"

// Explicit f32 transcendental operations. Variant transports their results as
// doubles, while arguments and computation retain the requested storage width.
class WScalarMath : public Object {
	GDCLASS(WScalarMath, Object);

protected:
	static void _bind_methods();

public:
	static float sqrt32(float p_value);
	static float reciprocal_sqrt_estimate32(float p_value);
};
