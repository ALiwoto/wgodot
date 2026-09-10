// wgodot-changes::file

#include "export_strings.h"

#include "export_context.h"
#include "export_maps.h"

#include "core/io/marshalls.h"

#include "modules/gdscript/wgodot_gd/obfuscation_format.h"
#include "modules/gdscript/wgodot_gd/string_obfuscation_format.h"

namespace WGodotGDScriptStringObfuscation {
using namespace WGodotGDScriptStringFormat;
using WGodotGDScriptFormat::STRING_MAP_PATH;

namespace {

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
} // namespace

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

} //namespace WGodotGDScriptStringObfuscation
