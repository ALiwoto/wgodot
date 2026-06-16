// wgodot-changes::file
/**************************************************************************/
/*  builtin_class_aliases.h                                               */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptExportTransform {
class ExportContext;
}

namespace WGodotGDScriptBuiltinClassAliases {

String get_alias_map_path();
Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context);
StringName resolve_alias(const StringName &p_name);
StringName resolve_function_alias(const StringName &p_name);
StringName resolve_member_alias(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property);
bool has_alias(const StringName &p_name);
bool has_function_alias(const StringName &p_name);
void clear_runtime_cache();

} // namespace WGodotGDScriptBuiltinClassAliases
