// wgodot-changes::file
#pragma once

#include "core/string/node_path.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"

namespace WGodotCppNames {

inline String path_text(const NodePath &p_path) {
	// NodePath's String conversion fills mutable caches shared by copies.
	// Native export workers only read shared paths; join their components directly.
	String result = p_path.is_absolute() ? "/" : "";
	for (int i = 0; i < p_path.get_name_count(); i++) {
		result += (i ? "/" : "") + String(p_path.get_name(i));
	}
	for (int i = 0; i < p_path.get_subname_count(); i++) {
		result += ":" + String(p_path.get_subname(i));
	}
	return result;
}

inline String quoted(const String &p_text) {
	return "\"" + p_text.c_escape() + "\"";
}

inline String symbol(const StringName &p_name) {
	const String name = p_name;
	// Avoid C++ keywords and reserved double underscores while retaining readable names.
	if (name.is_valid_ascii_identifier() && !name.contains("__")) {
		return "gd" + name;
	}
	const Vector<uint8_t> bytes = name.to_utf8_buffer();
	return "encoded" + String::hex_encode_buffer(bytes.ptr(), bytes.size());
}

} // namespace WGodotCppNames
