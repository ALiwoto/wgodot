// wgodot-changes::file
/**************************************************************************/
/*  interface_method_aliases.cpp                                          */
/**************************************************************************/

#include "interface_method_aliases.h"

#include "obfuscation_format.h"
#include "resource_map_codec.h"
#ifdef TOOLS_ENABLED
#include "script_resolution.h"
#endif

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/templates/hash_map.h"

namespace {

constexpr const char *ALIAS_MAP_PATH = WGodotGDScriptFormat::INTERFACE_ALIAS_MAP_PATH;

HashMap<uint64_t, StringName> builtin_method_aliases;
bool aliases_loaded = false;

void load_aliases() {
	if (aliases_loaded) {
		return;
	}

	aliases_loaded = true;
	if (!FileAccess::exists(ALIAS_MAP_PATH)) {
		return;
	}

	const Vector<uint8_t> encoded_data = FileAccess::get_file_as_bytes(ALIAS_MAP_PATH);
	const Vector<uint8_t> data = WGodotGDScriptResourceMapCodec::decode_resource_map(ALIAS_MAP_PATH, encoded_data);
	int offset = 0;
	while (offset + 8 <= data.size()) {
		const uint32_t interface_index = decode_uint32(&data[offset]);
		offset += 4;
		const uint32_t method_index = decode_uint32(&data[offset]);
		offset += 4;
		String alias;
		if (!WGodotGDScriptFormat::read_string(data, offset, alias)) {
			break;
		}
		if (!alias.is_empty()) {
			builtin_method_aliases[WGodotGDScriptFormat::interface_slot(interface_index, method_index)] = StringName(alias);
		}
	}
}

} // namespace

namespace WGodotGDScriptInterfaceMethodAliases {

String get_alias_map_path() {
	return ALIAS_MAP_PATH;
}

StringName resolve_builtin_alias(int p_interface_index, int p_method_index) {
#ifdef TOOLS_ENABLED
	StringName overridden;
	if (WGodotGDScriptResolution::resolve_interface_alias_override(p_interface_index, p_method_index, overridden)) {
		return overridden;
	}
#endif

	if (p_interface_index < 0 || p_method_index < 0) {
		return StringName();
	}
	load_aliases();
	const StringName *alias = builtin_method_aliases.getptr(WGodotGDScriptFormat::interface_slot(p_interface_index, p_method_index));
	return alias != nullptr ? *alias : StringName();
}

void clear_runtime_cache() {
	builtin_method_aliases.clear();
	aliases_loaded = false;
}

} // namespace WGodotGDScriptInterfaceMethodAliases
