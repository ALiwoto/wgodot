// wgodot-changes::file
/**************************************************************************/
/*  builtin_alias_resolver.cpp                                            */
/**************************************************************************/

#include "builtin_alias_resolver.h"

#include "builtin_class_aliases.h"

#include "core/object/class_db.h"
#include "core/variant/variant.h"

namespace WGodotGDScriptBuiltinAliasResolver {

StringName resolve_class_alias_or_name(const StringName &p_name) {
	const StringName resolved = WGodotGDScriptBuiltinClassAliases::resolve_alias(p_name);
	return !resolved.is_empty() ? resolved : p_name;
}

StringName get_owner_from_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_hard_type() || p_type.is_variant()) {
		return StringName();
	}

	if (p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type < Variant::VARIANT_MAX) {
		const StringName type_name = Variant::get_type_name(p_type.builtin_type);
		return type_name == SNAME("Variant") ? StringName() : type_name;
	}

	if (!p_type.native_type.is_empty() && ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type)) {
		return p_type.native_type;
	}

	return StringName();
}

StringName get_owner_from_codegen_type(const GDScriptDataType &p_type) {
	if (!p_type.has_type()) {
		return StringName();
	}

	if (p_type.kind == GDScriptDataType::BUILTIN && p_type.builtin_type < Variant::VARIANT_MAX) {
		const StringName type_name = Variant::get_type_name(p_type.builtin_type);
		return type_name == SNAME("Variant") ? StringName() : type_name;
	}

	if (!p_type.native_type.is_empty() && ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type)) {
		return p_type.native_type;
	}

	return StringName();
}

StringName resolve_member_alias_for_codegen(const GDScriptDataType &p_type, const StringName &p_name, bool p_static, bool p_property) {
	const StringName owner = get_owner_from_codegen_type(p_type);
	StringName resolved = WGodotGDScriptBuiltinClassAliases::resolve_member_alias(owner, p_name, p_static, p_property);
	if (resolved.is_empty() && p_property) {
		resolved = WGodotGDScriptBuiltinClassAliases::resolve_member_alias(owner, p_name, !p_static, p_property);
	}
	return !resolved.is_empty() ? resolved : p_name;
}

} // namespace WGodotGDScriptBuiltinAliasResolver
