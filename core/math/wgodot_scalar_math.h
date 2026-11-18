// wgodot-changes::file
#pragma once

namespace WScalarMath32 {

float sqrt(float p_value);
float reciprocal_sqrt_estimate(float p_value);
float reciprocal_sqrt_refined(float p_value);
float atan2_approx(float p_y, float p_x);
float asin_approx(float p_value);

} // namespace WScalarMath32
