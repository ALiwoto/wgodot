// wgodot-changes::file
#pragma once

#include "core/math/projection.h"

namespace CameraProjection {

// Keep the finite projection for spatial partitions, shadows and effect ranges.
// Only rasterization/depth reconstruction use this infinite perspective limit.
inline Projection for_rendering(const Projection &p_finite, bool p_infinite_far) {
	Projection result = p_finite;
	if (p_infinite_far && !p_finite.is_orthogonal()) {
		const real_t clip_near = p_finite.get_z_near();
		result.columns[2][2] = -1;
		result.columns[3][2] = -2 * clip_near;
	}
	return result;
}

} // namespace CameraProjection
