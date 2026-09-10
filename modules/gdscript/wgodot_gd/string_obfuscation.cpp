// wgodot-changes::file
/**************************************************************************/
/*  string_obfuscation.cpp                                                */
/**************************************************************************/

#include "string_obfuscation.h"

#include "obfuscation_format.h"
#include "resource_map_codec.h"
#include "string_obfuscation_format.h"
#ifdef TOOLS_ENABLED
#include "script_resolution.h"
#endif

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/string/string_builder.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

#include <array>

namespace {

using WGodotGDScriptFormat::STRING_MAP_PATH;
using namespace WGodotGDScriptStringFormat;

HashMap<uint64_t, String> string_resources;
bool string_resources_loaded = false;

struct Base52Digit {
	char32_t character;
	uint8_t value;
};

constexpr auto make_sorted_digits() {
	std::array<Base52Digit, BASE52_SIZE> digits{};
	for (int i = 0; i < BASE52_SIZE; i++) {
		digits[i] = { BASE52_ALPHABET[i], static_cast<uint8_t>(i) };
		for (int j = i; j > 0 && digits[j].character < digits[j - 1].character; j--) {
			const Base52Digit previous = digits[j - 1];
			digits[j - 1] = digits[j];
			digits[j] = previous;
		}
	}
	return digits;
}

constexpr auto BASE52_DIGITS = make_sorted_digits();

int decode_digit(char32_t p_character) {
	int low = 0;
	int high = BASE52_SIZE;
	while (low < high) {
		const int middle = (low + high) / 2;
		if (BASE52_DIGITS[middle].character < p_character) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	return low < BASE52_SIZE && BASE52_DIGITS[low].character == p_character ? BASE52_DIGITS[low].value : -1;
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

	const HashMap<uint64_t, String> *resources = nullptr;
#ifdef TOOLS_ENABLED
	resources = WGodotGDScriptResolution::get_string_resources_override();
#endif
	if (resources == nullptr) {
		ensure_string_resources_loaded();
		resources = &string_resources;
	}

	StringBuilder decoded;
	const char32_t *characters = p_marker.ptr();
	const int end = p_marker.length() - 2;
	int offset = 2;
	bool found = false;
	while (offset < end) {
		// The writer separates IDs with backslashes; retain the existing empty-segment behavior.
		if (characters[offset] == '\\') {
			offset++;
			continue;
		}
		uint64_t id = 0;
		while (offset < end && characters[offset] != '\\') {
			const int digit = decode_digit(characters[offset++]);
			if (digit < 0 || id > (UINT64_MAX - digit) / BASE52_SIZE) {
				return false;
			}
			id = id * BASE52_SIZE + digit;
		}
		const String *fragment = resources->getptr(id);
		if (fragment == nullptr) {
			return false;
		}
		decoded.append(*fragment);
		found = true;
	}
	if (!found) {
		return false;
	}
	*r_decoded = decoded.as_string();
	return true;
}

} // namespace

namespace WGodotGDScriptStringObfuscation {

String get_string_map_path() {
	return STRING_MAP_PATH;
}

void clear_runtime_cache() {
	string_resources.clear();
	string_resources_loaded = false;
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
