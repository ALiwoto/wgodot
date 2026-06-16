// wgodot-changes::file
/**************************************************************************/
/*  builtin_class_aliases.h                                               */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptBuiltinClassAliases {

String get_alias_map_path();
Vector<uint8_t> serialize_alias_map(const HashMap<StringName, StringName> &p_native_to_alias, const HashMap<StringName, StringName> &p_function_to_alias);
StringName resolve_alias(const StringName &p_name);
StringName resolve_function_alias(const StringName &p_name);
bool has_alias(const StringName &p_name);
bool has_function_alias(const StringName &p_name);
void clear_runtime_cache();

} // namespace WGodotGDScriptBuiltinClassAliases
