// wgodot-changes::file
/**************************************************************************/
/*  string_obfuscation.cpp                                                */
/**************************************************************************/

#include "string_obfuscation.h"

#include "export_context.h"
#include "resource_map_codec.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

namespace {

constexpr const char *STRING_MAP_PATH = "res://.godot/wss.a";

HashMap<uint64_t, String> string_resources;
bool string_resources_loaded = false;

const char32_t BASE52_ALPHABET[] = {
	0x0009, 0x000A, 0x000D, 0x001B, 0x00A0, 0x00AD, 0x034F, 0x061C, 0x070F, 0x115F, 0x1160, 0x180E,
	0x200B, 0x200C, 0x200D, 0x200E, 0x200F, 0x2028, 0x2029, 0x202A, 0x202B, 0x202C, 0x202D, 0x202E,
	0x202F, 0x2060, 0x2061, 0x2062, 0x2063, 0x2064, 0x2066, 0x2067, 0x2068, 0x2069, 0x206A, 0x206B,
	0x206C, 0x206D, 0x206E, 0x206F, 0x2800, 0x3164, 0xFE00, 0xFE0E, 0xFE0F, 0xFEFF,
	0x3042, 0x3044, 0x3046, 0x3048, 0x304A, 0x304B,
};

constexpr int BASE52_SIZE = sizeof(BASE52_ALPHABET) / sizeof(BASE52_ALPHABET[0]);
static_assert(BASE52_SIZE == 52);

String get_literal_prefix(Variant::Type p_type) {
	switch (p_type) {
		case Variant::STRING_NAME:
			return "&";
		case Variant::NODE_PATH:
			return "^";
		case Variant::STRING:
			return String();
		default:
			return String();
	}
}

bool is_supported_literal_type(Variant::Type p_type) {
	return p_type == Variant::STRING || p_type == Variant::STRING_NAME || p_type == Variant::NODE_PATH;
}

String unicode_escape(char32_t p_char) {
	if (p_char <= 0xFFFF) {
		return "\\u" + String::num_uint64(p_char, 16).lpad(4, "0");
	}
	return "\\U" + String::num_uint64(p_char, 16).lpad(6, "0");
}

String encode_base52(uint64_t p_value) {
	if (p_value == 0) {
		return String::chr(BASE52_ALPHABET[0]);
	}

	String result;
	while (p_value > 0) {
		const uint64_t digit = p_value % BASE52_SIZE;
		result = String::chr(BASE52_ALPHABET[digit]) + result;
		p_value /= BASE52_SIZE;
	}
	return result;
}

bool decode_base52(const String &p_text, uint64_t *r_value) {
	ERR_FAIL_NULL_V(r_value, false);
	if (p_text.is_empty()) {
		return false;
	}

	uint64_t value = 0;
	for (int i = 0; i < p_text.length(); i++) {
		int digit = -1;
		for (int j = 0; j < BASE52_SIZE; j++) {
			if (p_text[i] == BASE52_ALPHABET[j]) {
				digit = j;
				break;
			}
		}
		if (digit < 0) {
			return false;
		}

		value = value * BASE52_SIZE + digit;
	}

	*r_value = value;
	return true;
}

int hex_value(char32_t p_char) {
	if (p_char >= '0' && p_char <= '9') {
		return p_char - '0';
	}
	if (p_char >= 'a' && p_char <= 'f') {
		return p_char - 'a' + 10;
	}
	if (p_char >= 'A' && p_char <= 'F') {
		return p_char - 'A' + 10;
	}
	return -1;
}

bool read_hex_codepoint(const String &p_text, int p_start, int p_count, char32_t *r_codepoint) {
	ERR_FAIL_NULL_V(r_codepoint, false);
	if (p_start < 0 || p_count <= 0 || p_start + p_count > p_text.length()) {
		return false;
	}

	char32_t value = 0;
	for (int i = 0; i < p_count; i++) {
		const int digit = hex_value(p_text[p_start + i]);
		if (digit < 0) {
			return false;
		}
		value = (value << 4) | digit;
	}

	*r_codepoint = value;
	return true;
}

String encode_fragment_text(const String &p_text, WGodotGDScriptExportTransform::ExportContext &r_context) {
	const int escape_offset = r_context.get_random_uint(2);
	String encoded;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t ch = p_text[i];
		const bool use_escape = ((i + escape_offset) % 2) == 0 || ch == '\\' || ch < 32 || ch == 0x7F;
		if (use_escape) {
			encoded += unicode_escape(ch);
		} else {
			encoded += String::chr(ch);
		}
	}
	return encoded;
}

String decode_fragment_text(const String &p_text) {
	String decoded;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t ch = p_text[i];
		if (ch != '\\' || i + 1 >= p_text.length()) {
			decoded += String::chr(ch);
			continue;
		}

		const char32_t escape_type = p_text[i + 1];
		const int hex_len = escape_type == 'U' ? 6 : (escape_type == 'u' ? 4 : 0);
		if (hex_len == 0) {
			decoded += String::chr(ch);
			continue;
		}

		char32_t codepoint = 0;
		if (!read_hex_codepoint(p_text, i + 2, hex_len, &codepoint)) {
			decoded += String::chr(ch);
			continue;
		}

		decoded += String::chr(codepoint);
		i += hex_len + 1;
	}
	return decoded;
}

void append_uint32(Vector<uint8_t> &r_output, uint32_t p_value) {
	const int offset = r_output.size();
	r_output.resize(offset + 4);
	encode_uint32(p_value, &r_output.write[offset]);
}

void append_uint64(Vector<uint8_t> &r_output, uint64_t p_value) {
	const int offset = r_output.size();
	r_output.resize(offset + 8);
	encode_uint64(p_value, &r_output.write[offset]);
}

void ensure_string_resources_loaded() {
	if (string_resources_loaded) {
		return;
	}

	string_resources_loaded = true;
	string_resources.clear();

	if (!FileAccess::exists(STRING_MAP_PATH)) {
		return;
	}

	const Vector<uint8_t> encoded_data = FileAccess::get_file_as_bytes(STRING_MAP_PATH);
	if (encoded_data.is_empty()) {
		return;
	}

	const Vector<uint8_t> data = WGodotGDScriptResourceMapCodec::decode_resource_map(STRING_MAP_PATH, encoded_data);
	if (data.is_empty()) {
		return;
	}

	int offset = 0;
	while (offset + 12 <= data.size()) {
		const uint64_t id = decode_uint64(&data[offset]);
		offset += 8;
		const uint32_t length = decode_uint32(&data[offset]);
		offset += 4;
		if (length > static_cast<uint32_t>(data.size() - offset)) {
			break;
		}

		const String encoded_fragment = length == 0 ? String() : String::utf8(reinterpret_cast<const char *>(&data.ptr()[offset]), length);
		offset += length;
		string_resources[id] = decode_fragment_text(encoded_fragment);
	}
}

bool decode_marker(const String &p_marker, String *r_decoded) {
	ERR_FAIL_NULL_V(r_decoded, false);
	if (!p_marker.begins_with("\\0") || !p_marker.ends_with("\\0") || p_marker.length() < 4) {
		return false;
	}

	ensure_string_resources_loaded();

	const String payload = p_marker.substr(2, p_marker.length() - 4);
	const Vector<String> ids = payload.split("\\", false);
	if (ids.is_empty()) {
		return false;
	}

	String decoded;
	for (const String &id_text : ids) {
		uint64_t id = 0;
		if (!decode_base52(id_text, &id)) {
			return false;
		}

		const String *fragment = string_resources.getptr(id);
		if (fragment == nullptr) {
			return false;
		}
		decoded += *fragment;
	}

	*r_decoded = decoded;
	return true;
}

} // namespace

namespace WGodotGDScriptStringObfuscation {

String get_string_map_path() {
	return STRING_MAP_PATH;
}

Vector<uint8_t> serialize_string_map(const WGodotGDScriptExportTransform::ExportContext &p_context) {
	Vector<uint8_t> output;
	for (const KeyValue<uint64_t, String> &resource : p_context.get_string_resources()) {
		append_uint64(output, resource.key);
		const Vector<uint8_t> value = resource.value.to_utf8_buffer();
		append_uint32(output, value.size());
		const int offset = output.size();
		output.resize(offset + value.size());
		for (int i = 0; i < value.size(); i++) {
			output.write[offset + i] = value[i];
		}
	}
	return WGodotGDScriptResourceMapCodec::encode_resource_map(STRING_MAP_PATH, output);
}

void clear_runtime_cache() {
	string_resources.clear();
	string_resources_loaded = false;
}

String make_uncached_obfuscated_string_literal_source(WGodotGDScriptExportTransform::ExportContext &r_context, Variant::Type p_type, const String &p_value) {
	if (!is_supported_literal_type(p_type) || p_value.is_empty()) {
		return String();
	}

	if (p_value.length() == 1) {
		return make_single_character_string_literal_source(p_type, p_value);
	}

	const int part_count = MIN(p_value.length(), 3 + static_cast<int>(r_context.get_random_uint(28)));
	Vector<uint64_t> ids;
	ids.resize(part_count);

	int start = 0;
	for (int i = 0; i < part_count; i++) {
		const int remaining_chars = p_value.length() - start;
		const int remaining_parts = part_count - i;
		int part_length = remaining_chars;
		if (remaining_parts > 1) {
			const int max_part_length = remaining_chars - remaining_parts + 1;
			part_length = 1 + static_cast<int>(r_context.get_random_uint(max_part_length));
		}

		const String encoded_fragment = encode_fragment_text(p_value.substr(start, part_length), r_context);
		ids.write[i] = r_context.create_string_resource(encoded_fragment);
		start += part_length;
	}

	String literal = get_literal_prefix(p_type) + "\"\\0";
	for (int i = 0; i < ids.size(); i++) {
		if (i > 0) {
			literal += "\\";
		}
		literal += encode_base52(ids[i]);
	}
	literal += "\\0\"";
	return literal;
}

String make_single_character_string_literal_source(Variant::Type p_type, const String &p_value) {
	if (!is_supported_literal_type(p_type) || p_value.length() != 1) {
		return String();
	}
	return get_literal_prefix(p_type) + "\"" + unicode_escape(p_value[0]) + "\"";
}

Variant decode_obfuscated_literal(const Variant &p_literal) {
	const Variant::Type type = p_literal.get_type();
	if (!is_supported_literal_type(type)) {
		return p_literal;
	}

	String marker;
	switch (type) {
		case Variant::STRING:
			marker = p_literal;
			break;
		case Variant::STRING_NAME:
			marker = String(StringName(p_literal));
			break;
		case Variant::NODE_PATH:
			marker = String(NodePath(p_literal));
			break;
		default:
			return p_literal;
	}

	String decoded;
	if (!decode_marker(marker, &decoded)) {
		return p_literal;
	}

	switch (type) {
		case Variant::STRING:
			return decoded;
		case Variant::STRING_NAME:
			return StringName(decoded);
		case Variant::NODE_PATH:
			return NodePath(decoded);
		default:
			return p_literal;
	}
}

} // namespace WGodotGDScriptStringObfuscation
