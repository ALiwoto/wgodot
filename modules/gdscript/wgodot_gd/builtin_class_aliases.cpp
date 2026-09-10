// wgodot-changes::file
/**************************************************************************/
/*  builtin_class_aliases.cpp                                             */
/**************************************************************************/

#include "builtin_class_aliases.h"

#include "obfuscation_format.h"
#include "resource_map_codec.h"
#ifdef TOOLS_ENABLED
#include "script_resolution.h"
#endif

#include "../gdscript_parser.h"
#include "../gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/templates/list.h"
#include "core/variant/variant.h"

namespace {

constexpr const char *ALIAS_MAP_PATH = WGodotGDScriptFormat::BUILTIN_ALIAS_MAP_PATH;
using WGodotGDScriptFormat::AliasRecordKind;

HashMap<StringName, StringName> alias_to_native;
HashMap<StringName, StringName> alias_to_function;
HashMap<StringName, StringName> instance_method_aliases;
HashMap<StringName, StringName> static_method_aliases;
HashMap<StringName, StringName> instance_property_aliases;
HashMap<StringName, StringName> static_property_aliases;
bool aliases_loaded = false;

bool is_supported_builtin_alias_target(const StringName &p_name) {
	if (p_name.is_empty()) {
		return false;
	}

	if (ClassDB::class_exists(p_name) && ClassDB::is_class_exposed(p_name)) {
		return true;
	}

	return GDScriptParser::get_builtin_type(p_name) < Variant::VARIANT_MAX;
}

bool is_supported_builtin_function_alias_target(const StringName &p_name) {
	return !p_name.is_empty() && (Variant::has_utility_function(p_name) || GDScriptUtilityFunctions::function_exists(p_name));
}

bool split_qualified_member(const StringName &p_qualified_name, StringName *r_owner, StringName *r_name) {
	ERR_FAIL_NULL_V(r_owner, false);
	ERR_FAIL_NULL_V(r_name, false);

	const String qualified_name = p_qualified_name;
	const int separator = qualified_name.rfind("::");
	if (separator <= 0 || separator >= qualified_name.length() - 2) {
		return false;
	}

	*r_owner = StringName(qualified_name.substr(0, separator));
	*r_name = StringName(qualified_name.substr(separator + 2));
	return !r_owner->is_empty() && !r_name->is_empty();
}

bool is_supported_builtin_member_alias_target(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return false;
	}

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_owner);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (p_property) {
			if (p_static) {
				return Variant::has_constant(builtin_type, p_name) || Variant::has_enum(builtin_type, p_name) || Variant::get_enum_for_enumeration(builtin_type, p_name) != StringName();
			}

			return Variant::has_member(builtin_type, p_name);
		}

		return Variant::has_builtin_method(builtin_type, p_name);
	}

	if (!ClassDB::class_exists(p_owner) || !ClassDB::is_class_exposed(p_owner)) {
		return false;
	}

	if (p_property) {
		if (ClassDB::has_property(p_owner, p_name)) {
			return true;
		}
		if (p_static) {
			if (ClassDB::has_enum(p_owner, p_name)) {
				return true;
			}
			bool valid = false;
			(void)ClassDB::get_integer_constant(p_owner, p_name, &valid);
			return valid;
		}
		return false;
	}

	MethodInfo method_info;
	if (!ClassDB::get_method_info(p_owner, p_name, &method_info)) {
		return false;
	}
	return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0 || Engine::get_singleton()->has_singleton(p_owner));
}

void read_member_alias_record(const StringName &p_alias, const StringName &p_target, bool p_static, bool p_property) {
	StringName owner;
	StringName member;
	if (!split_qualified_member(p_target, &owner, &member) || !is_supported_builtin_member_alias_target(owner, member, p_static, p_property)) {
		return;
	}

	HashMap<StringName, StringName> &aliases = p_property ? (p_static ? static_property_aliases : instance_property_aliases) : (p_static ? static_method_aliases : instance_method_aliases);
	// Aliases are globally unique across owners in the exporter.
	aliases[p_alias] = member;
}

void load_aliases() {
	if (aliases_loaded) {
		return;
	}

	aliases_loaded = true;
	if (!FileAccess::exists(ALIAS_MAP_PATH)) {
		return;
	}

	const Vector<uint8_t> encoded_data = FileAccess::get_file_as_bytes(ALIAS_MAP_PATH);
	if (encoded_data.is_empty()) {
		return;
	}

	const Vector<uint8_t> data = WGodotGDScriptResourceMapCodec::decode_resource_map(ALIAS_MAP_PATH, encoded_data);
	if (data.is_empty()) {
		return;
	}

	int offset = 0;
	while (offset < data.size()) {
		const AliasRecordKind kind = static_cast<AliasRecordKind>(data[offset++]);
		if (offset >= data.size() || data[offset++] != 0) {
			break;
		}
		String alias_string;
		String target_string;
		if (!WGodotGDScriptFormat::read_string(data, offset, alias_string) || !WGodotGDScriptFormat::read_string(data, offset, target_string)) {
			break;
		}

		const StringName alias(alias_string);
		const StringName target(target_string);
		if (alias.is_empty()) {
			continue;
		}

		if (kind == AliasRecordKind::NATIVE_TYPE && is_supported_builtin_alias_target(target)) {
			alias_to_native[alias] = target;
		} else if (kind == AliasRecordKind::BUILTIN_FUNCTION && is_supported_builtin_function_alias_target(target)) {
			alias_to_function[alias] = target;
		} else if (kind == AliasRecordKind::INSTANCE_METHOD) {
			read_member_alias_record(alias, target, false, false);
		} else if (kind == AliasRecordKind::STATIC_METHOD) {
			read_member_alias_record(alias, target, true, false);
		} else if (kind == AliasRecordKind::INSTANCE_PROPERTY) {
			read_member_alias_record(alias, target, false, true);
		} else if (kind == AliasRecordKind::STATIC_PROPERTY) {
			read_member_alias_record(alias, target, true, true);
		}
	}
}

} // namespace

namespace WGodotGDScriptBuiltinClassAliases {

String get_alias_map_path() {
	return ALIAS_MAP_PATH;
}

StringName resolve_alias(const StringName &p_name) {
#ifdef TOOLS_ENABLED
	StringName overridden;
	if (WGodotGDScriptResolution::resolve_native_alias_override(p_name, overridden)) {
		return overridden;
	}
#endif

	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const StringName *native = alias_to_native.getptr(p_name);
	return native != nullptr ? *native : StringName();
}

StringName resolve_function_alias(const StringName &p_name) {
#ifdef TOOLS_ENABLED
	StringName overridden;
	if (WGodotGDScriptResolution::resolve_function_alias_override(p_name, overridden)) {
		return overridden;
	}
#endif

	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const StringName *function = alias_to_function.getptr(p_name);
	return function != nullptr ? *function : StringName();
}

StringName resolve_member_alias(const StringName &p_name, bool p_static, bool p_property) {
#ifdef TOOLS_ENABLED
	StringName overridden;
	if (WGodotGDScriptResolution::resolve_member_alias_override(p_name, p_static, p_property, overridden)) {
		return overridden;
	}
#endif

	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const HashMap<StringName, StringName> &aliases = p_property ? (p_static ? static_property_aliases : instance_property_aliases) : (p_static ? static_method_aliases : instance_method_aliases);
	const StringName *member = aliases.getptr(p_name);
	return member != nullptr ? *member : StringName();
}

bool has_alias(const StringName &p_name) {
	return !resolve_alias(p_name).is_empty();
}

bool has_function_alias(const StringName &p_name) {
	return !resolve_function_alias(p_name).is_empty();
}

void clear_runtime_cache() {
	alias_to_native.clear();
	alias_to_function.clear();
	instance_method_aliases.clear();
	static_method_aliases.clear();
	instance_property_aliases.clear();
	static_property_aliases.clear();
	aliases_loaded = false;
}

} // namespace WGodotGDScriptBuiltinClassAliases
