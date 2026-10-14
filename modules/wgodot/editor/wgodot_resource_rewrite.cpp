// wgodot-changes::file
#include "wgodot_resource_rewrite.h"

#include "core/string/string_builder.h"

namespace {
PropertyInfo container_element_property(const PropertyInfo &p_container, int p_index = 0) {
	PropertyInfo property;
	if (p_container.hint != PROPERTY_HINT_TYPE_STRING && p_container.hint != PROPERTY_HINT_ARRAY_TYPE && p_container.hint != PROPERTY_HINT_DICTIONARY_TYPE) {
		return property;
	}
	String hint = p_container.hint_string;
	if (p_container.type == Variant::DICTIONARY) {
		const PackedStringArray types = hint.split(";", true, 1);
		if (p_index >= types.size()) {
			return property;
		}
		hint = types[p_index];
	}
	// Container hints encode each element as type/hint:hint_string.
	const int separator = hint.find_char(':');
	if (separator >= 0) {
		const String type = hint.substr(0, separator);
		property.type = Variant::Type(type.get_slicec('/', 0).to_int());
		property.hint = PropertyHint(type.get_slicec('/', 1).to_int());
		property.hint_string = hint.substr(separator + 1);
	}
	return property;
}
} // namespace

Variant WGodotResourceRewrite::value(const Variant &p_value, const PropertyInfo &p_property, const PathMapper &p_path) {
	// Directory settings describe locations, not resources in the pack catalog.
	if (p_property.hint == PROPERTY_HINT_DIR || p_property.hint == PROPERTY_HINT_GLOBAL_DIR) {
		return p_value;
	}
	switch (p_value.get_type()) {
		case Variant::STRING:
		case Variant::STRING_NAME: {
			const String value = p_value;
			String replacement = value;
			if (value.begins_with("res://") || value.begins_with("uid://")) {
				int suffix = value.find("::", 6);
				if (suffix < 0) {
					suffix = value.rfind(":"); // Translation remap locale suffix.
				}
				replacement = suffix > 5 ? p_path(value.substr(0, suffix)) + value.substr(suffix) : p_path(value);
			} else if (value.begins_with("*res://") || value.begins_with("*uid://")) {
				replacement = "*" + p_path(value.substr(1));
			}
			return p_value.get_type() == Variant::STRING_NAME ? Variant(StringName(replacement)) : Variant(replacement);
		}
		case Variant::PACKED_STRING_ARRAY: {
			PackedStringArray result = p_value;
			const PropertyInfo element = container_element_property(p_property);
			for (int i = 0; i < result.size(); i++) {
				result.set(i, WGodotResourceRewrite::value(result[i], element, p_path));
			}
			return result;
		}
		case Variant::ARRAY: {
			const Array source = p_value;
			Array result;
			const PropertyInfo element = container_element_property(p_property);
			for (const Variant &value : source) {
				result.push_back(WGodotResourceRewrite::value(value, element, p_path));
			}
			return result;
		}
		case Variant::DICTIONARY: {
			const Dictionary source = p_value;
			Dictionary result;
			const PropertyInfo key_property = container_element_property(p_property);
			const PropertyInfo value_property = container_element_property(p_property, 1);
			for (const KeyValue<Variant, Variant> &entry : source) {
				result[WGodotResourceRewrite::value(entry.key, key_property, p_path)] = WGodotResourceRewrite::value(entry.value, value_property, p_path);
			}
			return result;
		}
		default:
			return p_value;
	}
}

String WGodotResourceRewrite::text(const String &p_text, const String &p_source, const PathMapper &p_path) {
	// Rewrite complete string tokens, preserving all other serialized values.
	// Shader include operands and external resource paths may be relative.
	StringBuilder result;
	const bool script = p_source.get_extension() == "gd";
	const bool moved_source = p_path(p_source) != p_source;
	const char32_t *characters = p_text.ptr();
	const int length = p_text.length();
	int copied = 0;
	for (int i = 0; i < length; i++) {
		if (script && characters[i] == '#') {
			while (i < length && characters[i] != '\n') {
				i++;
			}
			continue;
		}
		if (characters[i] != '"' && (!script || characters[i] != '\'')) {
			continue;
		}
		const char32_t quote = characters[i];
		const int quote_length = script && i + 2 < length && characters[i + 1] == quote && characters[i + 2] == quote ? 3 : 1;
		const int begin = i;
		i += quote_length;
		while (i < length) {
			if (characters[i] == quote && (quote_length == 1 || (i + 2 < length && characters[i + 1] == quote && characters[i + 2] == quote))) {
				break;
			}
			if (characters[i] == '\\') {
				i++;
			}
			i++;
		}
		if (i >= length) {
			break;
		}
		const String value = p_text.substr(begin + quote_length, i - begin - quote_length).c_unescape();
		i += quote_length - 1;
		String replacement = value;
		const int line_begin = p_text.rfind("\n", begin) + 1;
		const String prefix = p_text.substr(line_begin, begin - line_begin).strip_edges();
		if (prefix.ends_with("uid=") && value.begins_with("uid://")) {
			continue; // UID attributes remain numeric identities; the UID cache is rewritten.
		} else if (value.begins_with("res://") || value.begins_with("uid://")) {
			replacement = WGodotResourceRewrite::value(value, PropertyInfo(), p_path);
		} else if ((prefix.ends_with("path=") && prefix.begins_with("[ext_resource")) || prefix == "#include" || (script && (prefix.ends_with("preload(") || prefix.ends_with("load(") || prefix == "extends"))) {
			const String resolved = p_source.get_base_dir().path_join(value).simplify_path();
			const String mapped = p_path(resolved);
			if (mapped != resolved || moved_source) {
				replacement = mapped;
			}
		} else if (value.contains("#include")) {
			replacement = WGodotResourceRewrite::text(value, p_source, p_path);
		}
		if (replacement != value) {
			result += p_text.substr(copied, begin - copied);
			const String delimiter = String::chr(quote).repeat(quote_length);
			result += delimiter + replacement.c_escape() + delimiter;
			copied = i + 1;
		}
	}
	result += p_text.substr(copied);
	return result.as_string();
}
