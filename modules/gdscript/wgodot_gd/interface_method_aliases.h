// wgodot-changes::file
/**************************************************************************/
/*  interface_method_aliases.h                                            */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptInterfaceMethodAliases {

String get_alias_map_path();
StringName resolve_builtin_alias(int p_interface_index, int p_method_index);
void clear_runtime_cache();

} // namespace WGodotGDScriptInterfaceMethodAliases
