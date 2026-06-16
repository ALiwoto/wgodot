// wgodot-changes::file
/**************************************************************************/
/*  builtin_class_aliases.cpp                                             */
/**************************************************************************/

#include "builtin_class_aliases.h"

#include "../gdscript_utility_functions.h"
#include "../gdscript_parser.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

namespace {

constexpr const char *ALIAS_MAP_PATH = "res://.godot/wgbca.a";

HashMap<StringName, StringName> alias_to_native;
HashMap<StringName, StringName> alias_to_function;
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

bool read_record_string(const Vector<uint8_t> &p_data, int &r_offset, String *r_string) {
	ERR_FAIL_NULL_V(r_string, false);

	Vector<uint8_t> string_bytes;
	while (r_offset < p_data.size()) {
		const uint8_t byte = p_data[r_offset++];
		if (byte == 0) {
			*r_string = string_bytes.is_empty() ? String() : String::utf8(reinterpret_cast<const char *>(string_bytes.ptr()), string_bytes.size());
			return true;
		}
		string_bytes.push_back(byte);
	}

	return false;
}

void append_alias_record(Vector<uint8_t> &r_output, uint8_t p_kind, const StringName &p_target, const StringName &p_alias) {
	if (p_target.is_empty() || p_alias.is_empty()) {
		return;
	}

	r_output.push_back(p_kind);
	r_output.push_back(0);
	const Vector<uint8_t> alias = String(p_alias).to_utf8_buffer();
	for (uint8_t byte : alias) {
		if (byte != 0) {
			r_output.push_back(byte);
		}
	}
	r_output.push_back(0);
	const Vector<uint8_t> target = String(p_target).to_utf8_buffer();
	for (uint8_t byte : target) {
		if (byte != 0) {
			r_output.push_back(byte);
		}
	}
	r_output.push_back(0);
}

void load_aliases() {
	if (aliases_loaded) {
		return;
	}

	aliases_loaded = true;
	if (!FileAccess::exists(ALIAS_MAP_PATH)) {
		return;
	}

	const Vector<uint8_t> data = FileAccess::get_file_as_bytes(ALIAS_MAP_PATH);
	if (data.is_empty()) {
		return;
	}

	int offset = 0;
	while (offset < data.size()) {
		const uint8_t kind = data[offset++];
		if (offset >= data.size() || data[offset++] != 0) {
			break;
		}
		String alias_string;
		String target_string;
		if (!read_record_string(data, offset, &alias_string) || !read_record_string(data, offset, &target_string)) {
			break;
		}

		const StringName alias(alias_string);
		const StringName target(target_string);
		if (alias.is_empty()) {
			continue;
		}

		if (kind == 'c' && is_supported_builtin_alias_target(target)) {
			alias_to_native[alias] = target;
		} else if (kind == 'f' && is_supported_builtin_function_alias_target(target)) {
			alias_to_function[alias] = target;
		}
	}
}

} // namespace

namespace WGodotGDScriptBuiltinClassAliases {

String get_alias_map_path() {
	return ALIAS_MAP_PATH;
}

Vector<uint8_t> serialize_alias_map(const HashMap<StringName, StringName> &p_native_to_alias, const HashMap<StringName, StringName> &p_function_to_alias) {
	Vector<uint8_t> output;
	for (const KeyValue<StringName, StringName> &native_alias : p_native_to_alias) {
		append_alias_record(output, 'c', native_alias.key, native_alias.value);
	}
	for (const KeyValue<StringName, StringName> &function_alias : p_function_to_alias) {
		append_alias_record(output, 'f', function_alias.key, function_alias.value);
	}

	return output;
}

StringName resolve_alias(const StringName &p_name) {
	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const StringName *native = alias_to_native.getptr(p_name);
	return native != nullptr ? *native : StringName();
}

StringName resolve_function_alias(const StringName &p_name) {
	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const StringName *function = alias_to_function.getptr(p_name);
	return function != nullptr ? *function : StringName();
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
	aliases_loaded = false;
}

} // namespace WGodotGDScriptBuiltinClassAliases
