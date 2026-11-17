// wgodot-changes::file
#pragma once

#include "core/math/vector3.h"

// Equal-distance groups interleave by layer, then retain their member order.
// Instance/material priorities still take precedence over distance and layer.
struct GeometrySortGroup {
	uint64_t id = 0;
	Vector3 origin;
	int32_t order = 0;
	int32_t layer = 0;

	bool operator==(const GeometrySortGroup &p_other) const {
		return id == p_other.id && origin == p_other.origin && order == p_other.order && layer == p_other.layer;
	}
};
