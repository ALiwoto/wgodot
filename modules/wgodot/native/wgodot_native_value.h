// wgodot-changes::file
#pragma once

namespace WGodotNative {

template <class T>
T value_container(T p_value) {
	p_value.make_value();
	return p_value;
}

} // namespace WGodotNative
