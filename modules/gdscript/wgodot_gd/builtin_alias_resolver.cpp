// wgodot-changes::file
/**************************************************************************/
/*  builtin_alias_resolver.cpp                                            */
/**************************************************************************/

#include "builtin_alias_resolver.h"

#include "builtin_class_aliases.h"

namespace WGodotGDScriptBuiltinAliasResolver {

StringName resolve_class_alias_or_name(const StringName &p_name) {
	const StringName resolved = WGodotGDScriptBuiltinClassAliases::resolve_alias(p_name);
	return !resolved.is_empty() ? resolved : p_name;
}

StringName resolve_member_alias_for_codegen(const StringName &p_name, bool p_static, bool p_property) {
	StringName resolved = WGodotGDScriptBuiltinClassAliases::resolve_member_alias(p_name, p_static, p_property);
	if (resolved.is_empty() && p_property) {
		resolved = WGodotGDScriptBuiltinClassAliases::resolve_member_alias(p_name, !p_static, p_property);
	}
	return !resolved.is_empty() ? resolved : p_name;
}

} // namespace WGodotGDScriptBuiltinAliasResolver
