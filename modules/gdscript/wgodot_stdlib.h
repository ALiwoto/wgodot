// wgodot-changes::file
/**************************************************************************/
/*  wgodot_stdlib.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"
#include "core/string/ustring.h"

namespace WGodotGDScriptStdLib {

bool has_global_interface(const StringName &p_name);
String get_global_interface_path(const StringName &p_name);
bool has_script_path(const String &p_path);
String get_script_source(const String &p_path);
void register_global_classes();

} // namespace WGodotGDScriptStdLib
