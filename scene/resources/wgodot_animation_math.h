// wgodot-changes::file
#pragma once

#include "wgodot_animation_values.h"

namespace WAnimationMath {

template <typename T> struct ArrayTraits { static constexpr bool enabled = false; };
template <typename T> struct ArrayTraits<Vector<T>> {
	static constexpr bool enabled = true;
	using Element = T;
};
template <typename T> inline constexpr bool is_string = std::is_same_v<T, String> || std::is_same_v<T, StringName>;
template <typename T> inline constexpr bool is_vector = std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> || std::is_same_v<T, Vector4>;

template <typename T> struct BlendType { using Type = T; };
template <> struct BlendType<bool> { using Type = double; };
template <> struct BlendType<int64_t> { using Type = double; };
template <> struct BlendType<int32_t> { using Type = double; };
template <> struct BlendType<float> { using Type = double; };
template <> struct BlendType<Vector2i> { using Type = Vector2; };
template <> struct BlendType<Vector3i> { using Type = Vector3; };
template <> struct BlendType<Vector4i> { using Type = Vector4; };
template <> struct BlendType<Rect2i> { using Type = Rect2; };
template <> struct BlendType<String> { using Type = Vector<double>; };
template <> struct BlendType<StringName> { using Type = Vector<double>; };
template <> struct BlendType<WAnimationNumber> { using Type = double; };
template <typename T> struct BlendType<Vector<T>> { using Type = Vector<typename BlendType<T>::Type>; };
template <> struct BlendType<PackedStringArray> { using Type = PackedStringArray; };
template <typename T> using Blend = typename BlendType<T>::Type;

template <typename T> Blend<T> to_blend(const T &p_value) {
	if constexpr (is_string<T>) {
		String text = p_value;
		Vector<double> result;
		result.resize(text.length());
		for (int i = 0; i < text.length(); i++) {
			result.write[i] = float(text[i]);
		}
		return result;
	} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
		return p_value.as_double();
	} else if constexpr (std::is_same_v<T, Blend<T>>) {
		return p_value;
	} else if constexpr (ArrayTraits<T>::enabled) {
		Blend<T> result;
		result.resize(p_value.size());
		for (int i = 0; i < p_value.size(); i++) {
			if constexpr (std::is_same_v<T, PackedInt32Array>) {
				result.write[i] = float(p_value[i]);
			} else {
				result.write[i] = to_blend(p_value[i]);
			}
		}
		return result;
	} else {
		return Blend<T>(p_value);
	}
}

template <typename T> T from_blend(const Blend<T> &p_value) {
	if constexpr (std::is_same_v<T, bool>) {
		return real_t(p_value) >= 0.5;
	} else if constexpr (std::is_integral_v<T>) {
		return T(Math::round(double(p_value)));
	} else if constexpr (std::is_same_v<T, Vector2i> || std::is_same_v<T, Vector3i> || std::is_same_v<T, Vector4i> || std::is_same_v<T, Rect2i>) {
		return T(p_value.round());
	} else if constexpr (is_string<T>) {
		String text;
		for (double value : p_value) {
			char32_t character = char32_t(Math::round(float(value)));
			if (character == 0 || (character & 0xfffff800) == 0xd800 || character > 0x10ffff) {
				character = ' ';
			}
			text += character;
		}
		return T(text);
	} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
		return WAnimationNumber(p_value);
	} else if constexpr (std::is_same_v<T, Blend<T>>) {
		return p_value;
	} else if constexpr (ArrayTraits<T>::enabled) {
		T result;
		result.resize(p_value.size());
		for (int i = 0; i < p_value.size(); i++) {
			if constexpr (std::is_same_v<T, PackedInt32Array>) {
				result.write[i] = int32_t(Math::round(float(p_value[i])));
			} else {
				result.write[i] = from_blend<typename ArrayTraits<T>::Element>(p_value[i]);
			}
		}
		return result;
	} else {
		return T(p_value);
	}
}

template <typename T> T linear(const T &a, const T &b, float c);
template <typename T> T cubic(const T &pa, const T &a, const T &b, const T &pb, float c, real_t pat, real_t bt, real_t pbt);
template <typename T> T subtract(const T &a, const T &b);
template <typename T> T blend(const T &a, const T &b, float c);

// Match Godot's polygon-array behavior: extend with the shorter array's last
// element, except at exact endpoints. This is also used for string code points.
template <typename T, typename F> T array_pair(const T &a, const T &b, float c, bool p_subtract, F &&p_operation) {
	const int common = MIN(a.size(), b.size());
	const int count = p_subtract || (a.size() > b.size() ? !Math::is_equal_approx(c, 1.0f) : !Math::is_zero_approx(c)) ? MAX(a.size(), b.size()) : common;
	T result;
	result.resize(count);
	using E = typename ArrayTraits<T>::Element;
	const E last_a = a.is_empty() ? E() : a[a.size() - 1];
	const E last_b = b.is_empty() ? E() : b[b.size() - 1];
	for (int i = 0; i < count; i++) {
		result.write[i] = p_operation(i < a.size() ? a[i] : last_a, i < b.size() ? b[i] : last_b);
	}
	return result;
}

template <typename T> T linear(const T &a, const T &b, float c) {
	if constexpr (std::is_same_v<T, PackedByteArray>) {
		return c < 0.5f ? a : b;
	} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
		double value = Math::lerp(a.as_double(), b.as_double(), double(c));
		return a.is_integer && b.is_integer ? WAnimationNumber(int64_t(Math::round(value))) : WAnimationNumber(value);
	} else if constexpr (is_string<T>) {
		auto left = to_blend(a);
		auto right = to_blend(b);
		int size = int(Math::round(Math::lerp(double(left.size()), double(right.size()), double(c))));
		(left.size() > right.size() ? left : right).resize(size);
		auto result = array_pair(left, right, c, false, [c](double x, double y) { return Math::lerp(x, y, double(c)); });
		// Missing characters snap to the larger string rather than fade from NUL.
		int common = MIN(left.size(), right.size());
		for (int i = common; i < result.size(); i++) {
			result.write[i] = left.size() > right.size() ? left[i] : right[i];
		}
		return from_blend<T>(result);
	} else if constexpr (!std::is_same_v<T, Blend<T>>) {
		return from_blend<T>(linear(to_blend(a), to_blend(b), c));
	} else if constexpr (std::is_floating_point_v<T>) {
		return Math::lerp(a, b, T(c));
	} else if constexpr (is_vector<T> || std::is_same_v<T, Color> || std::is_same_v<T, Basis>) {
		return a.lerp(b, c);
	} else if constexpr (std::is_same_v<T, Quaternion>) {
		return a.slerp(b, c);
	} else if constexpr (std::is_same_v<T, Transform2D> || std::is_same_v<T, Transform3D>) {
		return a.interpolate_with(b, c);
	} else if constexpr (std::is_same_v<T, Rect2> || std::is_same_v<T, AABB>) {
		return T(a.position.lerp(b.position, c), a.size.lerp(b.size, c));
	} else if constexpr (std::is_same_v<T, Plane>) {
		return Plane(a.normal.lerp(b.normal, c), Math::lerp(double(a.d), double(b.d), double(c)));
	} else if constexpr (ArrayTraits<T>::enabled) {
		return array_pair(a, b, c, false, [c](const auto &x, const auto &y) { return linear(x, y, c); });
	} else {
		return c < 0.5f ? a : b;
	}
}

template <typename T> T cubic(const T &pa, const T &a, const T &b, const T &pb, float c, real_t pat, real_t bt, real_t pbt) {
	if constexpr (std::is_same_v<T, PackedByteArray>) {
		return c < 0.5f ? a : b;
	} else if constexpr (std::is_same_v<T, WAnimationNumber>) {
		double value = cubic(pa.as_double(), a.as_double(), b.as_double(), pb.as_double(), c, pat, bt, pbt);
		return pa.is_integer && a.is_integer && b.is_integer && pb.is_integer ? WAnimationNumber(int64_t(Math::round(value))) : WAnimationNumber(value);
	} else if constexpr (is_string<T>) {
		return linear(a, b, c);
	} else if constexpr (!std::is_same_v<T, Blend<T>>) {
		return from_blend<T>(cubic(to_blend(pa), to_blend(a), to_blend(b), to_blend(pb), c, pat, bt, pbt));
	} else if constexpr (std::is_floating_point_v<T>) {
		return Math::cubic_interpolate_in_time(a, b, pa, pb, T(c), T(bt), T(pat), T(pbt));
	} else if constexpr (is_vector<T>) {
		return a.cubic_interpolate_in_time(b, pa, pb, c, bt, pat, pbt);
	} else if constexpr (std::is_same_v<T, Quaternion>) {
		return a.spherical_cubic_interpolate_in_time(b, pa, pb, c, bt, pat, pbt);
	} else if constexpr (std::is_same_v<T, Color>) {
		T result;
		for (int i = 0; i < 4; i++) {
			result[i] = cubic(double(pa[i]), double(a[i]), double(b[i]), double(pb[i]), c, pat, bt, pbt);
		}
		return result;
	} else if constexpr (std::is_same_v<T, Rect2> || std::is_same_v<T, AABB>) {
		return T(cubic(pa.position, a.position, b.position, pb.position, c, pat, bt, pbt), cubic(pa.size, a.size, b.size, pb.size, c, pat, bt, pbt));
	} else if constexpr (std::is_same_v<T, Plane>) {
		return Plane(cubic(pa.normal, a.normal, b.normal, pb.normal, c, pat, bt, pbt), cubic(double(pa.d), double(a.d), double(b.d), double(pb.d), c, pat, bt, pbt));
	} else if constexpr (std::is_same_v<T, Basis>) {
		return Basis(cubic(pa.rows[0], a.rows[0], b.rows[0], pb.rows[0], c, pat, bt, pbt),
				cubic(pa.rows[1], a.rows[1], b.rows[1], pb.rows[1], c, pat, bt, pbt),
				cubic(pa.rows[2], a.rows[2], b.rows[2], pb.rows[2], c, pat, bt, pbt));
	} else if constexpr (std::is_same_v<T, Transform3D>) {
		T result;
		result.basis = cubic(pa.basis, a.basis, b.basis, pb.basis, c, pat, bt, pbt);
		result.origin = cubic(pa.origin, a.origin, b.origin, pb.origin, c, pat, bt, pbt);
		return result;
	} else if constexpr (std::is_same_v<T, Transform2D>) {
		T result;
		for (int i = 0; i < 3; i++) {
			result[i] = cubic(pa[i], a[i], b[i], pb[i], c, pat, bt, pbt);
		}
		return result;
	} else if constexpr (ArrayTraits<T>::enabled) {
		using E = typename ArrayTraits<T>::Element;
		const E pre_last = pa.is_empty() ? E() : pa[pa.size() - 1];
		const E post_last = pb.is_empty() ? E() : pb[pb.size() - 1];
		int i = 0;
		return array_pair(a, b, c, false, [&](const E &x, const E &y) {
			E value = cubic(i < pa.size() ? pa[i] : pre_last, x, y, i < pb.size() ? pb[i] : post_last, c, pat, bt, pbt);
			i++;
			return value;
		});
	} else {
		return c < 0.5f ? a : b;
	}
}

template <typename T> T subtract(const T &a, const T &b) {
	if constexpr (std::is_floating_point_v<T> || is_vector<T> || std::is_same_v<T, Color>) {
		return a - b;
	} else if constexpr (std::is_same_v<T, Quaternion> || std::is_same_v<T, Basis>) {
		return b.inverse() * a;
	} else if constexpr (std::is_same_v<T, Transform2D> || std::is_same_v<T, Transform3D>) {
		return b.affine_inverse() * a;
	} else if constexpr (std::is_same_v<T, Rect2> || std::is_same_v<T, AABB>) {
		return T(a.position - b.position, a.size - b.size);
	} else if constexpr (std::is_same_v<T, Plane>) {
		return Plane(a.normal - b.normal, a.d - b.d);
	} else if constexpr (ArrayTraits<T>::enabled) {
		return array_pair(a, b, 0, true, [](const auto &x, const auto &y) { return subtract(x, y); });
	} else {
		return a;
	}
}

template <typename T> T blend(const T &a, const T &b, float c) {
	if constexpr (is_string<T>) {
		auto left = to_blend(a);
		auto right = to_blend(b);
		int size = int(Math::round(Math::lerp(double(left.size()), double(right.size()), double(c))));
		(left.size() > right.size() ? left : right).resize(size);
		return from_blend<T>(blend(left, right, c));
	} else if constexpr (std::is_floating_point_v<T> || is_vector<T> || std::is_same_v<T, Color> || std::is_same_v<T, Basis>) {
		return a + b * c;
	} else if constexpr (std::is_same_v<T, Quaternion>) {
		return a * Quaternion().slerp(b, c);
	} else if constexpr (std::is_same_v<T, Transform2D> || std::is_same_v<T, Transform3D>) {
		return a * T().interpolate_with(b, c);
	} else if constexpr (std::is_same_v<T, Rect2> || std::is_same_v<T, AABB>) {
		return T(a.position + b.position * c, a.size + b.size * c);
	} else if constexpr (std::is_same_v<T, Plane>) {
		return Plane(a.normal + b.normal * c, a.d + b.d * c);
	} else if constexpr (ArrayTraits<T>::enabled) {
		return array_pair(a, b, c, false, [c](const auto &x, const auto &y) { return blend(x, y, c); });
	} else {
		return c < 0.5f ? a : b;
	}
}

template <typename T> T sample(const LocalVector<T> &p_values, const WAnimationSample &s) {
	const T &a = p_values[s.from];
	if (s.interpolation == 0) {
		return a;
	}
	const T &b = p_values[s.next];
	if constexpr (std::is_same_v<T, double> || std::is_same_v<T, WAnimationNumber>) {
		bool angle = s.interpolation == 3 || s.interpolation == 4;
		if constexpr (std::is_same_v<T, WAnimationNumber>) {
			angle &= !a.is_integer || !b.is_integer || (s.interpolation == 4 && (!p_values[s.pre].is_integer || !p_values[s.post].is_integer));
		}
		if (angle) {
			real_t value;
			if (s.interpolation == 3) {
				value = Math::lerp_angle(real_t(to_blend(a)), real_t(to_blend(b)), s.weight);
			} else {
				value = Math::cubic_interpolate_angle_in_time(real_t(to_blend(a)), real_t(to_blend(b)), real_t(to_blend(p_values[s.pre])), real_t(to_blend(p_values[s.post])), s.weight, s.next_time, s.pre_time, s.post_time);
			}
			return T(double(Math::fposmod(float(value), float(Math::TAU))));
		}
	}
	if (s.interpolation == 2 || s.interpolation == 4) {
		return cubic(p_values[s.pre], a, b, p_values[s.post], float(s.weight), s.pre_time, s.next_time, s.post_time);
	}
	return linear(a, b, float(s.weight));
}

template <typename T> Blend<T> sample_for_blending(const LocalVector<T> &p_values, const WAnimationSample &s) {
	if constexpr (std::is_same_v<T, PackedFloat32Array>) {
		// Generic interpolation promotes packed float elements to doubles. Keep
		// that precision until the final property write, just like the old mixer.
		const auto a = to_blend(p_values[s.from]);
		if (s.interpolation == 0) {
			return a;
		}
		const auto b = to_blend(p_values[s.next]);
		if (s.interpolation == 2 || s.interpolation == 4) {
			return cubic(to_blend(p_values[s.pre]), a, b, to_blend(p_values[s.post]), float(s.weight), s.pre_time, s.next_time, s.post_time);
		}
		return linear(a, b, float(s.weight));
	} else {
		return to_blend(sample(p_values, s));
	}
}
} // namespace WAnimationMath
