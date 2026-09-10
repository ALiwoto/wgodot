// wgodot-changes::file

#include "string_obfuscation_format.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptStringFormat {

namespace {

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
} // namespace

String decode_fragment_text(const String &p_text) {
	String decoded;
	decoded.resize_uninitialized(p_text.length() + 1);
	char32_t *buffer = decoded.ptrw();
	int written = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t ch = p_text[i];
		if (ch != '\\' || i + 1 >= p_text.length()) {
			buffer[written++] = ch;
			continue;
		}

		const char32_t escape_type = p_text[i + 1];
		const int hex_len = escape_type == 'U' ? 6 : (escape_type == 'u' ? 4 : 0);
		if (hex_len == 0) {
			buffer[written++] = ch;
			continue;
		}

		char32_t codepoint = 0;
		if (!read_hex_codepoint(p_text, i + 2, hex_len, &codepoint)) {
			buffer[written++] = ch;
			continue;
		}

		buffer[written++] = codepoint;
		i += hex_len + 1;
	}
	buffer[written] = 0;
	decoded.resize_uninitialized(written + 1);
	return decoded;
}

} //namespace WGodotGDScriptStringFormat
