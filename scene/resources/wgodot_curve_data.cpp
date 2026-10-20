// wgodot-changes::file
#include "curve.h"

void Curve::set_native_limits(real_t p_min_value, real_t p_max_value, real_t p_min_domain, real_t p_max_domain) {
	// Source deserialization also bypasses the interactive range constraints.
	_min_value = p_min_value;
	_max_value = p_max_value;
	_min_domain = p_min_domain;
	_max_domain = p_max_domain;
}

void Curve::set_native_points(LocalVector<Point> p_points) {
	const bool size_changed = p_points.size() != _points.size();
	_points = std::move(p_points);
	mark_dirty();
	if (size_changed) {
		notify_property_list_changed();
	}
}
