// wgodot-changes::file
/**************************************************************************/
/*  wgodot_stdlib.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/templates/local_vector.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"

namespace WGodotGDScriptStdLib {

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
