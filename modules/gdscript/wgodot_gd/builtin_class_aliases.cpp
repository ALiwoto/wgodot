// wgodot-changes::file
/**************************************************************************/
/*  builtin_class_aliases.cpp                                             */
/**************************************************************************/

#include "builtin_class_aliases.h"

#include "../gdscript_parser.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

namespace {

constexpr const char *ALIAS_MAP_PATH = "res://.godot/wgbca.a";

HashMap<StringName, StringName> alias_to_native;
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

void load_aliases() {
	if (aliases_loaded) {
		return;
	}

	aliases_loaded = true;
	if (!FileAccess::exists(ALIAS_MAP_PATH)) {
		return;
	}

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(ALIAS_MAP_PATH, FileAccess::READ, &err);
	if (err != OK || file.is_null()) {
		return;
	}

	while (!file->eof_reached()) {
		const String line = file->get_line().strip_edges();
		if (line.is_empty()) {
			continue;
		}

		const int separator = line.find("=");
		if (separator <= 0 || separator >= line.length() - 1) {
			continue;
		}

		const StringName alias = StringName(line.substr(0, separator).strip_edges());
		const StringName native = StringName(line.substr(separator + 1).strip_edges());
		if (alias.is_empty() || !is_supported_builtin_alias_target(native)) {
			continue;
		}

		alias_to_native[alias] = native;
	}
}

} // namespace

namespace WGodotGDScriptBuiltinClassAliases {

String get_alias_map_path() {
	return ALIAS_MAP_PATH;
}

Vector<uint8_t> serialize_alias_map(const HashMap<StringName, StringName> &p_native_to_alias) {
	String output;
	for (const KeyValue<StringName, StringName> &native_alias : p_native_to_alias) {
		if (native_alias.key.is_empty() || native_alias.value.is_empty()) {
			continue;
		}
		output += String(native_alias.value) + "=" + String(native_alias.key) + "\n";
	}

	return output.to_utf8_buffer();
}

StringName resolve_alias(const StringName &p_name) {
	if (p_name.is_empty()) {
		return StringName();
	}

	load_aliases();
	const StringName *native = alias_to_native.getptr(p_name);
	return native != nullptr ? *native : StringName();
}

bool has_alias(const StringName &p_name) {
	return !resolve_alias(p_name).is_empty();
}

void clear_runtime_cache() {
	alias_to_native.clear();
	aliases_loaded = false;
}

} // namespace WGodotGDScriptBuiltinClassAliases
