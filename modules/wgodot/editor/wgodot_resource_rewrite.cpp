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
	int copied = 0;
	for (int i = 0; i < p_text.length(); i++) {
		if (p_text[i] != '"') {
			continue;
		}
		const int begin = i++;
		while (i < p_text.length() && p_text[i] != '"') {
			if (p_text[i] == '\\') {
				i++;
			}
			i++;
		}
		if (i >= p_text.length()) {
			break;
		}
		const String value = p_text.substr(begin + 1, i - begin - 1).c_unescape();
		String replacement = value;
		const int line_begin = p_text.rfind("\n", begin) + 1;
		const String prefix = p_text.substr(line_begin, begin - line_begin).strip_edges();
		if (prefix.ends_with("uid=") && value.begins_with("uid://")) {
			continue; // UID attributes remain numeric identities; the UID cache is rewritten.
		} else if (value.begins_with("res://") || value.begins_with("uid://")) {
			replacement = WGodotResourceRewrite::value(value, PropertyInfo(), p_path);
		} else if ((prefix.ends_with("path=") && prefix.begins_with("[ext_resource")) || prefix == "#include") {
			replacement = p_path(p_source.get_base_dir().path_join(value).simplify_path());
		} else if (value.contains("#include")) {
			replacement = WGodotResourceRewrite::text(value, p_source, p_path);
		}
		if (replacement != value) {
			result += p_text.substr(copied, begin - copied);
			result += "\"" + replacement.c_escape() + "\"";
			copied = i + 1;
		}
	}
	result += p_text.substr(copied);
	return result.as_string();
}

