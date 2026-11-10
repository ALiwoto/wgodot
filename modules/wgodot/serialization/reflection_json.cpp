// wgodot-changes::file
#include "arrays.h"
#include "reflection.h"

#ifdef TOOLS_ENABLED
namespace WGodotSerialization {
Variant json_value(const Variant &p_value, const GDScriptDataType &p_type, Kind p_kind) {
	if (p_type.builtin_type == Variant::OBJECT) {
		Object *model = p_value;
		return model ? Variant(reflected_json(model)) : Variant();
	}
	if (is_sequence(p_type.builtin_type, p_kind)) {
		Variant collection = p_value;
		Array result;
		const auto element = sequence_element(p_type);
		visit_array(collection, [&](const auto &array) {
			result.resize(array.size());
			for (int64_t i = 0; i < array.size(); ++i) {
				result[i] = json_value(array[i], element);
			}
			return true;
		});
		return result;
	}
	if (p_type.builtin_type == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		Dictionary result;
		const Array keys = dictionary.keys();
		for (int64_t i = 0; i < keys.size(); ++i) {
			result[String(keys[i])] = json_value(dictionary[keys[i]], p_type.get_container_element_type(1));
		}
		return result;
	}
	if (p_type.builtin_type == Variant::VECTOR3) {
		const Vector3 vector = p_value;
		Array result;
		result.push_back(vector.x);
		result.push_back(vector.y);
		result.push_back(vector.z);
		return result;
	}
	if (p_type.builtin_type == Variant::PACKED_BYTE_ARRAY && p_kind == Kind::BYTES) {
		const PackedByteArray bytes = p_value;
		return encode_base64(bytes.ptr(), bytes.size());
	}
	return p_value;
}

bool parse_json_value(const Variant &p_value, const GDScriptDataType &p_type, Variant &r_value, Kind p_kind) {
	if (p_type.builtin_type == Variant::PACKED_BYTE_ARRAY && p_kind == Kind::BYTES) {
		PackedByteArray bytes;
		if (p_value.get_type() != Variant::STRING || !decode_base64(p_value, bytes)) {
			return false;
		}
		r_value = bytes;
		return true;
	}
	if (p_value.get_type() == Variant::NIL) {
		if (p_type.builtin_type != Variant::OBJECT) {
			return false;
		}
		r_value = Variant();
		return true;
	}
	if (p_type.builtin_type == Variant::OBJECT) {
		if (p_value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		Variant model = new_model(p_type);
		if (model.is_zero() || !reflected_parse_json(model, p_value)) {
			return false;
		}
		r_value = model;
		return true;
	}
	if (is_sequence(p_type.builtin_type, p_kind)) {
		if (p_value.get_type() != Variant::ARRAY) {
			return false;
		}
		const Array input = p_value;
		const auto element = sequence_element(p_type);
		return visit_array(r_value, [&](auto &output) {
			output.clear();
			for (int64_t i = 0; i < input.size(); ++i) {
				Variant item = default_value(element);
				if (!parse_json_value(input[i], element, item) || !fits_array(item, p_type.builtin_type)) {
					return false;
				}
				output.push_back(item);
			}
			return true;
		});
	}
	if (p_type.builtin_type == Variant::DICTIONARY) {
		if (p_value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		const Dictionary input = p_value;
		Dictionary output = r_value;
		output.clear();
		const auto key_type = p_type.get_container_element_type(0);
		const auto value_type = p_type.get_container_element_type(1);
		const Array keys = input.keys();
		for (int64_t i = 0; i < keys.size(); ++i) {
			Variant key = default_value(key_type);
			Variant value = default_value(value_type);
			if (key_type.builtin_type == Variant::BOOL) {
				const String text = keys[i];
				if (text != "true" && text != "false") {
					return false;
				}
				key = text == "true";
			} else if (!parse_json_value(keys[i], key_type, key)) {
				return false;
			}
			if (!parse_json_value(input[keys[i]], value_type, value)) {
				return false;
			}
			output[key] = value;
		}
		r_value = output;
		return true;
	}
	if (p_type.builtin_type == Variant::VECTOR3) {
		if (p_value.get_type() != Variant::ARRAY) {
			return false;
		}
		const Array input = p_value;
		if (input.size() != 3) {
			return false;
		}
		Vector3 vector;
		for (int i = 0; i < 3; ++i) {
			if (!input[i].is_num()) {
				return false;
			}
			vector[i] = double(input[i]);
		}
		r_value = vector;
		return true;
	}
	if (p_type.builtin_type == Variant::INT) {
		int64_t integer = 0;
		if (!json_integer(p_value, integer)) {
			return false;
		}
		r_value = integer;
		return true;
	}
	if (p_type.builtin_type == Variant::FLOAT) {
		if (!p_value.is_num() && (p_value.get_type() != Variant::STRING || !String(p_value).is_valid_float())) {
			return false;
		}
		r_value = double(p_value);
		return true;
	}
	if (p_value.get_type() != p_type.builtin_type) {
		return false;
	}
	r_value = p_value;
	return true;
}
} // namespace WGodotSerialization

Dictionary WGodotSerialization::reflected_json(const Object *p_object) {
	Dictionary result;
	for (const auto &field : WGodotSerialization::reflected_fields(p_object, false, true)) {
		result[field.json] = WGodotSerialization::json_value(p_object->get(field.name), field.type, field.kind);
	}
	return result;
}

bool WGodotSerialization::reflected_parse_json(Object *p_object, const Dictionary &p_data) {
	for (const auto &field : WGodotSerialization::reflected_fields(p_object, false, true)) {
		const Variant *input = p_data.getptr(field.json);
		if (!input) {
			continue;
		}
		Variant value = p_object->get(field.name);
		if (!WGodotSerialization::parse_json_value(*input, field.type, value, field.kind)) {
			return false;
		}
		p_object->set(field.name, value);
	}
	return true;
}
#endif
