// wgodot-changes::file

#include "export_context.h"
#include "export_maps.h"
#include "obfuscation_names.h"

#include "core/io/marshalls.h"

#include "modules/gdscript/wgodot_gd/obfuscation_format.h"
#include "modules/gdscript/wgodot_gd/resource_map_codec.h"

namespace {
using WGodotGDScriptFormat::AliasRecordKind;
void append_alias_record(Vector<uint8_t> &r_output, AliasRecordKind p_kind, const StringName &p_target, const StringName &p_alias) {
	if (p_target.is_empty() || p_alias.is_empty()) {
		return;
	}

	r_output.push_back(static_cast<uint8_t>(p_kind));
	r_output.push_back(0);
	const Vector<uint8_t> alias = WGodotGDScriptExportTransform::unwrap_binary_identifier_escape(String(p_alias)).to_utf8_buffer();
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
} // namespace

namespace WGodotGDScriptBuiltinClassAliases {
Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context) {
	Vector<uint8_t> output;
	for (const KeyValue<StringName, StringName> &native_alias : p_context.get_builtin_class_aliases()) {
		append_alias_record(output, AliasRecordKind::NATIVE_TYPE, native_alias.key, native_alias.value);
	}
	for (const KeyValue<StringName, StringName> &function_alias : p_context.get_builtin_function_aliases()) {
		append_alias_record(output, AliasRecordKind::BUILTIN_FUNCTION, function_alias.key, function_alias.value);
	}
	for (const KeyValue<StringName, StringName> &method_alias : p_context.get_builtin_instance_method_aliases()) {
		append_alias_record(output, AliasRecordKind::INSTANCE_METHOD, method_alias.key, method_alias.value);
	}
	for (const KeyValue<StringName, StringName> &method_alias : p_context.get_builtin_static_method_aliases()) {
		append_alias_record(output, AliasRecordKind::STATIC_METHOD, method_alias.key, method_alias.value);
	}
	for (const KeyValue<StringName, StringName> &property_alias : p_context.get_builtin_instance_property_aliases()) {
		append_alias_record(output, AliasRecordKind::INSTANCE_PROPERTY, property_alias.key, property_alias.value);
	}
	for (const KeyValue<StringName, StringName> &property_alias : p_context.get_builtin_static_property_aliases()) {
		append_alias_record(output, AliasRecordKind::STATIC_PROPERTY, property_alias.key, property_alias.value);
	}

	return WGodotGDScriptResourceMapCodec::encode_resource_map(WGodotGDScriptFormat::BUILTIN_ALIAS_MAP_PATH, output);
}
} //namespace WGodotGDScriptBuiltinClassAliases

namespace {
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
} //namespace

namespace WGodotGDScriptInterfaceMethodAliases {
Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context) {
	Vector<uint8_t> output;
	for (const KeyValue<uint64_t, StringName> &entry : p_context.get_builtin_interface_aliases()) {
		append_u32(output, entry.key >> 32);
		append_u32(output, entry.key & 0xffffffff);
		append_alias(output, WGodotGDScriptExportTransform::unwrap_binary_identifier_escape(entry.value));
	}
	return WGodotGDScriptResourceMapCodec::encode_resource_map(WGodotGDScriptFormat::INTERFACE_ALIAS_MAP_PATH, output);
}
} //namespace WGodotGDScriptInterfaceMethodAliases
