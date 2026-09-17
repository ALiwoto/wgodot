// wgodot-changes::file
#pragma once
#include "core/object/wgodot_interface_registry.h"

namespace WGodotGDScriptStdLib {
using NativeInterface = WGodotNativeInterfaces::Descriptor;
inline const NativeInterface *get_native_interface(const StringName &p_name) {
	return WGodotNativeInterfaces::get_descriptor(p_name);
}
inline bool has_global_interface(const StringName &p_name) {
	return get_native_interface(p_name) != nullptr;
}
inline void get_global_interface_list(LocalVector<StringName> &r_names) {
	WGodotNativeInterfaces::get_descriptor_list(r_names);
}
void register_global_types();
void clear_module_interfaces();
} // namespace WGodotGDScriptStdLib
