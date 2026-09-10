// wgodot-changes::file
/**************************************************************************/
/*  builtin_alias_resolver.h                                              */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"

namespace WGodotGDScriptBuiltinAliasResolver {

StringName resolve_class_alias_or_name(const StringName &p_name);
StringName resolve_member_alias_for_codegen(const StringName &p_name, bool p_static, bool p_property);

} // namespace WGodotGDScriptBuiltinAliasResolver
