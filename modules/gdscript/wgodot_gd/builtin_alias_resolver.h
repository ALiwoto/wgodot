// wgodot-changes::file
/**************************************************************************/
/*  builtin_alias_resolver.h                                              */
/**************************************************************************/

#pragma once

#include "../gdscript_function.h"
#include "../gdscript_parser.h"

#include "core/string/string_name.h"

namespace WGodotGDScriptBuiltinAliasResolver {

StringName resolve_class_alias_or_name(const StringName &p_name);
StringName get_owner_from_datatype(const GDScriptParser::DataType &p_type);
StringName get_owner_from_codegen_type(const GDScriptDataType &p_type);
StringName resolve_member_alias_for_codegen(const GDScriptDataType &p_type, const StringName &p_name, bool p_static, bool p_property);

} // namespace WGodotGDScriptBuiltinAliasResolver
