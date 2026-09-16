// wgodot-changes::file
/**************************************************************************/
/*  wgodot_stdlib.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/object/method_info.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/variant/type_info.h"

#include <type_traits>

namespace WGodotGDScriptStdLib {

struct NativeInterface {
	StringName name;
	StringName native_base;
	String cpp_type;
	String cpp_header;
	StringName api_class;
	List<MethodInfo> methods;
};

template <class C, class R, class... Args>
MethodInfo interface_method(const StringName &p_name, R (C::*p_method)(Args...)) {
	MethodInfo method(GetTypeInfo<std::decay_t<R>>::get_class_info(), p_name);
	(method.arguments.push_back(GetTypeInfo<std::decay_t<Args>>::get_class_info()), ...);
	return method;
}

String make_interface_source(const NativeInterface &p_interface);
Error register_interface(const NativeInterface &p_interface);
const NativeInterface *get_native_interface(const StringName &p_name);
void register_native_implementation(const StringName &p_class, const StringName &p_interface);
void initialize_native_interfaces();
void clear_module_interfaces();
bool has_global_interface(const StringName &p_name);
String get_global_interface_path(const StringName &p_name);
void get_global_interface_list(LocalVector<StringName> &r_interfaces);
int get_builtin_interface_count();
int get_builtin_interface_index(const StringName &p_name);
String get_builtin_interface_path(int p_index);
String get_builtin_interface_source(int p_index);
bool has_script_path(const String &p_path);
String get_script_source(const String &p_path);
void register_global_classes();

} // namespace WGodotGDScriptStdLib
