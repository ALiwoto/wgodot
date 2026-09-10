// wgodot-changes::file
/**************************************************************************/
/*  interface_method_aliases.cpp                                          */
/**************************************************************************/

#include "interface_method_aliases.h"

#include "export_context.h"
#include "export_analysis.h"
#include "obfuscation_names.h"
#include "resource_map_codec.h"


#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/templates/hash_map.h"

namespace {

constexpr const char *ALIAS_MAP_PATH = "res://.godot/wgima.a";

HashMap<uint64_t, StringName> builtin_method_aliases;
bool aliases_loaded = false;

uint64_t make_slot_key(uint32_t p_interface_index, uint32_t p_method_index) {
	return (static_cast<uint64_t>(p_interface_index) << 32) | p_method_index;
}

void append_u32(Vector<uint8_t> &r_output, uint32_t p_value) {
	const int offset = r_output.size();
	r_output.resize(offset + 4);
	encode_uint32(p_value, &r_output.write[offset]);
}

void append_alias(Vector<uint8_t> &r_output, const String &p_value) {
	const Vector<uint8_t> bytes = p_value.to_utf8_buffer();
	for (uint8_t byte : bytes) {
		if (byte != 0) {
			r_output.push_back(byte);
		}
	}
	r_output.push_back(0);
}

bool read_alias(const Vector<uint8_t> &p_data, int &r_offset, String *r_value) {
	ERR_FAIL_NULL_V(r_value, false);

	Vector<uint8_t> bytes;
	while (r_offset < p_data.size()) {
		const uint8_t byte = p_data[r_offset++];
		if (byte == 0) {
			*r_value = bytes.is_empty() ? String() : String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
			return true;
		}
		bytes.push_back(byte);
	}
	return false;
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
	const Vector<uint8_t> data = WGodotGDScriptResourceMapCodec::decode_resource_map(ALIAS_MAP_PATH, encoded_data);
	int offset = 0;
	while (offset + 8 <= data.size()) {
		const uint32_t interface_index = decode_uint32(&data[offset]);
		offset += 4;
		const uint32_t method_index = decode_uint32(&data[offset]);
		offset += 4;
		String alias;
		if (!read_alias(data, offset, &alias)) {
			break;
		}
		if (!alias.is_empty()) {
			builtin_method_aliases[make_slot_key(interface_index, method_index)] = StringName(alias);
		}
	}
}

} // namespace

namespace WGodotGDScriptInterfaceMethodAliases {

String get_alias_map_path() {
	return ALIAS_MAP_PATH;
}

Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context) {
	Vector<uint8_t> output;
	for (const KeyValue<uint64_t, StringName> &entry : p_context.get_builtin_interface_aliases()) {
		append_u32(output, entry.key >> 32);
		append_u32(output, entry.key & 0xffffffff);
		append_alias(output, WGodotGDScriptExportTransform::unwrap_binary_identifier_escape(entry.value));
	}
	return WGodotGDScriptResourceMapCodec::encode_resource_map(ALIAS_MAP_PATH, output);
}

StringName resolve_builtin_alias(int p_interface_index, int p_method_index) {
	if (auto *analysis = WGodotGDScriptExportTransform::ExportAnalysis::get_active()) {
		const StringName *alias = analysis->get_artifacts().get_builtin_interface_aliases().getptr(make_slot_key(p_interface_index, p_method_index));
		return alias != nullptr ? *alias : StringName();
	}

	if (p_interface_index < 0 || p_method_index < 0) {
		return StringName();
	}
	load_aliases();
	const StringName *alias = builtin_method_aliases.getptr(make_slot_key(p_interface_index, p_method_index));
	return alias != nullptr ? *alias : StringName();
}

void clear_runtime_cache() {
	builtin_method_aliases.clear();
	aliases_loaded = false;
}

} // namespace WGodotGDScriptInterfaceMethodAliases
