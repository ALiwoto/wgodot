// wgodot-changes::file
#pragma once

#include "core/math/vector3.h"

// Explicit double precision without changing real_t or existing geometry.
struct [[nodiscard]] alignas(8) WVector3d {
	static constexpr int AXIS_COUNT = 3;
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	constexpr WVector3d() = default;
	constexpr WVector3d(double p_x, double p_y, double p_z) : x(p_x), y(p_y), z(p_z) {
	}
	explicit WVector3d(const Vector3 &p_value) : x(p_value.x), y(p_value.y), z(p_value.z) {
	}

	double &operator[](int p_axis) {
		DEV_ASSERT((unsigned int)p_axis < AXIS_COUNT);
		if (p_axis == 0) {
			return x;
		}
		return p_axis == 1 ? y : z;
	}
	const double &operator[](int p_axis) const {
		DEV_ASSERT((unsigned int)p_axis < AXIS_COUNT);
		if (p_axis == 0) {
			return x;
		}
		return p_axis == 1 ? y : z;
	}
	WVector3d operator+(const WVector3d &p_value) const {
		return WVector3d(x + p_value.x, y + p_value.y, z + p_value.z);
	}
	WVector3d operator-(const WVector3d &p_value) const {
		return WVector3d(x - p_value.x, y - p_value.y, z - p_value.z);
	}
	WVector3d operator*(const WVector3d &p_value) const {
		return WVector3d(x * p_value.x, y * p_value.y, z * p_value.z);
	}
	WVector3d operator/(const WVector3d &p_value) const {
		return WVector3d(x / p_value.x, y / p_value.y, z / p_value.z);
	}
	WVector3d operator*(double p_value) const {
		return WVector3d(x * p_value, y * p_value, z * p_value);
	}
	WVector3d operator/(double p_value) const {
		return WVector3d(x / p_value, y / p_value, z / p_value);
	}
	WVector3d operator-() const {
		return WVector3d(-x, -y, -z);
	}
	bool operator==(const WVector3d &p_value) const {
		return x == p_value.x && y == p_value.y && z == p_value.z;
	}
	bool operator!=(const WVector3d &p_value) const {
		return !(*this == p_value);
	}
	bool is_same(const WVector3d &p_value) const {
		return Math::is_same(x, p_value.x) && Math::is_same(y, p_value.y) && Math::is_same(z, p_value.z);
	}
	bool is_finite() const {
		return Math::is_finite(x) && Math::is_finite(y) && Math::is_finite(z);
	}

	double dot(const WVector3d &p_value) const {
		return x * p_value.x + y * p_value.y + z * p_value.z;
	}
	WVector3d cross(const WVector3d &p_value) const {
		return WVector3d(y * p_value.z - z * p_value.y, z * p_value.x - x * p_value.z, x * p_value.y - y * p_value.x);
	}
	double length_squared() const {
		return dot(*this);
	}
	double length() const {
		return Math::sqrt(length_squared());
	}
	double distance_squared_to(const WVector3d &p_value) const {
		return (*this - p_value).length_squared();
	}
	double distance_to(const WVector3d &p_value) const {
		return (*this - p_value).length();
	}
	WVector3d normalized(double p_tolerance = 0.0) const {
		const double squared = length_squared();
		if (squared == 1.0) {
			return *this;
		}
		if (squared == 0.0 || squared < p_tolerance) {
			return WVector3d();
		}
		return *this * (1.0 / Math::sqrt(squared));
	}
	WVector3d lerp(const WVector3d &p_to, double p_weight) const {
		return *this + (p_to - *this) * p_weight;
	}
	Vector3 to_vector3() const {
		return Vector3(x, y, z);
	}
	uint32_t hash() const;
	operator String() const;
};

inline WVector3d operator*(double p_scalar, const WVector3d &p_value) {
	return p_value * p_scalar;
}
