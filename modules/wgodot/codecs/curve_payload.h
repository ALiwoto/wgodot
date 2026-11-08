// wgodot-changes::file
#pragma once

#include "native_payload.h"

#include "scene/resources/curve.h"

namespace WGodotNativePayload {
constexpr uint32_t CURVE_POINTS = 2;

template <>
struct Value<Curve::Point> {
	static Curve::Point read(Input &p_input) {
		Curve::Point point;
		point.position = Value<Vector2>::read(p_input);
		point.left_tangent = Value<real_t>::read(p_input);
		point.right_tangent = Value<real_t>::read(p_input);
		point.left_mode = Curve::TangentMode(Value<int32_t>::read(p_input));
		point.right_mode = Curve::TangentMode(Value<int32_t>::read(p_input));
		return point;
	}
	static void write(FileAccess &p_file, const Curve::Point &p_point) {
		Value<Vector2>::write(p_file, p_point.position);
		Value<real_t>::write(p_file, p_point.left_tangent);
		Value<real_t>::write(p_file, p_point.right_tangent);
		Value<int32_t>::write(p_file, p_point.left_mode);
		Value<int32_t>::write(p_file, p_point.right_mode);
	}
};

template <>
struct Value<LocalVector<Curve::Point>> : LocalValues<Curve::Point, sizeof(real_t) * 4 + sizeof(int32_t) * 2> {};
} // namespace WGodotNativePayload
