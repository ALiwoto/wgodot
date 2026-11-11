// wgodot-changes::file
#include "wgodot_resource_value.h"

#include "core/object/class_db.h"
#include "core/object/script_instance.h"
#include "core/variant/variant_internal.h"

#include <limits>
#include <type_traits>

bool WGodotResourceDataType::concrete() const {
	if (builtin <= Variant::NIL || builtin >= Variant::VARIANT_MAX || builtin == Variant::CALLABLE || builtin == Variant::SIGNAL || builtin == Variant::RID) {
		return false;
	}
	if (builtin == Variant::OBJECT && name.is_empty()) {
		return false;
	}
	const int count = builtin == Variant::ARRAY ? 1 : builtin == Variant::DICTIONARY ? 2
																					 : 0;
	if (elements.size() != count) {
		return false;
	}
	for (const WGodotResourceDataType &element : elements) {
		if (!element.concrete()) {
			return false;
		}
	}
	return true;
}

String WGodotResourceDataType::text() const {
	String result = resource_path ? String("WResPath") : builtin == Variant::OBJECT ? String(name)
																					: Variant::get_type_name(builtin);
	if (!elements.is_empty()) {
		Vector<String> parameters;
		for (const WGodotResourceDataType &element : elements) {
			parameters.push_back(element.text());
		}
		result += "[" + String(", ").join(parameters) + "]";
	}
	return result;
}

WGodotResourceDataType WGodotResourceDataType::from_property(const PropertyInfo &p_info) {
	WGodotResourceDataType result;
	result.builtin = p_info.type;
	if (result.builtin == Variant::OBJECT) {
		result.name = p_info.class_name;
		if (result.name.is_empty() && (p_info.hint == PROPERTY_HINT_RESOURCE_TYPE || p_info.hint == PROPERTY_HINT_NODE_TYPE)) {
			result.name = p_info.hint_string;
		}
	}
	if (result.builtin == Variant::ARRAY || result.builtin == Variant::DICTIONARY) {
		const int count = result.builtin == Variant::ARRAY ? 1 : 2;
		const Vector<String> parameters = p_info.hint_string.split(";", false);
		if ((p_info.hint == PROPERTY_HINT_ARRAY_TYPE || p_info.hint == PROPERTY_HINT_DICTIONARY_TYPE || p_info.hint == PROPERTY_HINT_TYPE_STRING) && parameters.size() == count) {
			for (const String &parameter : parameters) {
				PropertyInfo element;
				if (parameter.contains(":")) {
					const String code = parameter.get_slice(":", 0);
					element.type = Variant::Type(code.get_slice("/", 0).to_int());
					element.hint = PropertyHint(code.get_slice("/", 1).to_int());
					element.hint_string = parameter.substr(parameter.find(":") + 1);
				} else {
					element.type = Variant::get_type_by_name(parameter);
					if (element.type == Variant::VARIANT_MAX && parameter != "Variant") {
						element.type = Variant::OBJECT;
						element.class_name = parameter;
					}
				}
				result.elements.push_back(from_property(element));
			}
		}
	}
	return result;
}

WGodotResourceDataType WGodotResourceDataType::packed_element(Variant::Type p_type) {
	WGodotResourceDataType result;
	switch (p_type) {
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
			result.builtin = Variant::INT;
			break;
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
			result.builtin = Variant::FLOAT;
			break;
		case Variant::PACKED_STRING_ARRAY:
			result.builtin = Variant::STRING;
			break;
		case Variant::PACKED_VECTOR2_ARRAY:
			result.builtin = Variant::VECTOR2;
			break;
		case Variant::PACKED_VECTOR3_ARRAY:
			result.builtin = Variant::VECTOR3;
			break;
		case Variant::PACKED_VECTOR4_ARRAY:
			result.builtin = Variant::VECTOR4;
			break;
		case Variant::PACKED_COLOR_ARRAY:
			result.builtin = Variant::COLOR;
			break;
		default:
			break;
	}
	return result;
}

namespace {
using VP = VariantParser;

template <class T>
Error packed_element(const Variant &p_value, Variant::Type p_element, String &r_error) {
	if (p_value.get_type() != p_element && !(p_element == Variant::FLOAT && p_value.get_type() == Variant::INT)) {
		r_error = "Packed array requires " + Variant::get_type_name(p_element) + " elements.";
		return ERR_PARSE_ERROR;
	}
	if constexpr (std::is_integral_v<T>) {
		const int64_t number = p_value;
		if (number < std::numeric_limits<T>::min() || number > std::numeric_limits<T>::max()) {
			r_error = "Packed array element is out of range.";
			return ERR_PARSE_ERROR;
		}
	}
	return OK;
}

template <class T>
Error packed_repeat(Variant &r_value, const Variant &p_value, Variant::Type p_element, int p_count, String &r_error) {
	RETURN_IF_ERROR(packed_element<T>(p_value, p_element, r_error));
	Vector<T> values;
	RETURN_IF_ERROR(values.resize(p_count));
	values.fill(p_value);
	VariantInternal::initialize_packed_storage(&r_value, values);
	return OK;
}

template <class T>
Error packed_literal(Variant &r_value, Variant::Type p_element, VP::Stream *p_stream, int &r_line, String &r_error, VP::ResourceParser *p_resources) {
	Vector<T> values;
	T batch[1024];
	int count = 0;
	auto flush = [&]() -> Error {
		const int64_t offset = values.size();
		RETURN_IF_ERROR(values.resize(offset + count));
		T *destination = values.ptrw() + offset;
		for (int i = 0; i < count; i++) {
			destination[i] = batch[i];
		}
		count = 0;
		return OK;
	};
	VP::Token token;
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	while (token.type != VP::TK_BRACKET_CLOSE) {
		Variant value;
		RETURN_IF_ERROR(VP::parse_value(token, value, p_stream, r_line, r_error, p_resources));
		RETURN_IF_ERROR(packed_element<T>(value, p_element, r_error));
		batch[count++] = value;
		if (count == 1024) {
			RETURN_IF_ERROR(flush());
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
		if (token.type == VP::TK_BRACKET_CLOSE) {
			break;
		}
		if (token.type != VP::TK_COMMA) {
			r_error = "Expected ',' or ']' in packed array literal.";
			return ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	}
	if (count) {
		RETURN_IF_ERROR(flush());
	}
	VariantInternal::initialize_packed_storage(&r_value, values);
	return OK;
}
} // namespace

Error WGodotResourceValueParser::parse_repeat_count(VP::Stream *p_stream, int &r_count, int &r_line, String &r_error) {
	VP::Token token;
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	if (token.type != VP::TK_COMMA) {
		r_error = "Expected ',' between repeat value and count.";
		return ERR_PARSE_ERROR;
	}
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	if (token.type != VP::TK_NUMBER || token.value.get_type() != Variant::INT || int64_t(token.value) < 0 || int64_t(token.value) > INT32_MAX) {
		r_error = "repeat count must be an integer from 0 to 2147483647.";
		return ERR_PARSE_ERROR;
	}
	r_count = int64_t(token.value);
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	if (token.type != VP::TK_PARENTHESIS_CLOSE) {
		r_error = "Expected ')' after repeat count.";
		return ERR_PARSE_ERROR;
	}
	return OK;
}

WGodotResourceValueParser::WGodotResourceValueParser(const StringName &p_native, const Ref<Script> &p_script, WGodotNamedTypes::Resolver &p_resolver, VP::AssignmentParser *p_custom) :
		resolver(p_resolver), custom(p_custom) {
	List<PropertyInfo> properties;
	if (!p_native.is_empty()) {
		ClassDB::get_property_list(p_native, &properties);
	}
	if (p_script.is_valid()) {
		p_script->get_script_property_list(&properties);
	}
	for (const PropertyInfo &property : properties) {
		Type type;
		// Inspector hints can be overridden with @export_custom. The script's
		// compiled declaration remains authoritative for container element types.
		if (p_script.is_null() || !p_script->wgodot_get_declared_property_type(property.name, type)) {
			type = Type::from_property(property);
		}
		fields.insert(property.name, type);
	}
}

Error WGodotResourceValueParser::parse(const String &p_name, VP::Token &p_token, VP::Stream *p_stream, int &r_line, String &r_error, VP::ResourceParser *p_resources) {
	if (custom) {
		const Error error = custom->parse(p_name, p_token, p_stream, r_line, r_error, p_resources);
		handled = custom->handled;
		if (error != OK || handled) {
			return error;
		}
	}
	if (initializing && initializing->get_script_instance()) {
		Variant *slot = initializing->get_script_instance()->wgodot_packed_initialization_slot(p_name);
		const Type *type = fields.getptr(p_name);
		if (slot && type) {
			RETURN_IF_ERROR(value(p_token, *slot, *type, p_stream, r_line, r_error, p_resources));
			if (slot->get_type() != type->builtin) {
				r_error = "Packed resource initializer does not match its declared field type.";
				return ERR_PARSE_ERROR;
			}
			handled = true;
		}
	}
	return OK;
}

Error WGodotResourceValueParser::parse_value(const String &p_name, VP::Token &p_token, Variant &r_value, VP::Stream *p_stream, int &r_line, String &r_error, VP::ResourceParser *p_resources) {
	const Type *type = fields.getptr(p_name);
	if (!type) {
		return VP::parse_value(p_token, r_value, p_stream, r_line, r_error, p_resources, true);
	}
	return value(p_token, r_value, *type, p_stream, r_line, r_error, p_resources);
}

Ref<Script> WGodotResourceValueParser::element_script(const Type &p_type) {
	return p_type.builtin == Variant::OBJECT && ScriptServer::is_global_class(p_type.name) ? resolver.script(p_type.name) : Ref<Script>();
}

Error WGodotResourceValueParser::value(VP::Token &p_token, Variant &r_value, const Type &p_type, VP::Stream *p_stream, int &r_line, String &r_error, VP::ResourceParser *p_resources, int p_depth) {
	if (p_depth > 64) {
		r_error = "Resource value nesting is too deep.";
		return ERR_PARSE_ERROR;
	}
	if (p_token.type == VP::TK_IDENTIFIER && String(p_token.value) == "repeat") {
		const bool packed = p_type.builtin >= Variant::PACKED_BYTE_ARRAY && p_type.builtin < Variant::VARIANT_MAX;
		if (!p_type.concrete() || (!packed && p_type.builtin != Variant::ARRAY)) {
			r_error = "repeat requires a fully declared array destination.";
			return ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		if (p_token.type != VP::TK_PARENTHESIS_OPEN) {
			r_error = "Expected '(' after repeat.";
			return ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		const Type element = packed ? Type::packed_element(p_type.builtin) : p_type.elements[0];
		Variant item;
		RETURN_IF_ERROR(value(p_token, item, element, p_stream, r_line, r_error, p_resources, p_depth + 1));
		int count;
		RETURN_IF_ERROR(parse_repeat_count(p_stream, count, r_line, r_error));
		switch (p_type.builtin) {
#define WG_PACKED_REPEAT(m_kind, m_type) \
	case Variant::m_kind: \
		return packed_repeat<m_type>(r_value, item, element.builtin, count, r_error);
			WG_PACKED_REPEAT(PACKED_BYTE_ARRAY, uint8_t)
			WG_PACKED_REPEAT(PACKED_INT32_ARRAY, int32_t)
			WG_PACKED_REPEAT(PACKED_INT64_ARRAY, int64_t)
			WG_PACKED_REPEAT(PACKED_FLOAT32_ARRAY, float)
			WG_PACKED_REPEAT(PACKED_FLOAT64_ARRAY, double)
			WG_PACKED_REPEAT(PACKED_STRING_ARRAY, String)
			WG_PACKED_REPEAT(PACKED_VECTOR2_ARRAY, Vector2)
			WG_PACKED_REPEAT(PACKED_VECTOR3_ARRAY, Vector3)
			WG_PACKED_REPEAT(PACKED_VECTOR4_ARRAY, Vector4)
			WG_PACKED_REPEAT(PACKED_COLOR_ARRAY, Color)
#undef WG_PACKED_REPEAT
			default:
				break;
		}
		Array array;
		const Ref<Script> script = element_script(element);
		array.set_typed(element.builtin, script.is_valid() ? script->get_instance_base_type() : element.name, script);
		array.push_back(item);
		if (array.size() != 1) {
			r_error = "repeat value does not match the declared element type.";
			return ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(array.resize(count));
		array.fill(item);
		r_value = array;
		return OK;
	}
	if (p_token.type != VP::TK_BRACKET_OPEN && p_token.type != VP::TK_CURLY_BRACKET_OPEN) {
		return VP::parse_value(p_token, r_value, p_stream, r_line, r_error, p_resources, true);
	}
	if (p_token.type == VP::TK_BRACKET_OPEN && p_type.builtin >= Variant::PACKED_BYTE_ARRAY && p_type.builtin < Variant::VARIANT_MAX) {
		const Variant::Type element = Type::packed_element(p_type.builtin).builtin;
		switch (p_type.builtin) {
#define WG_PACKED_LITERAL(m_kind, m_type) \
	case Variant::m_kind: \
		return packed_literal<m_type>(r_value, element, p_stream, r_line, r_error, p_resources);
			WG_PACKED_LITERAL(PACKED_BYTE_ARRAY, uint8_t)
			WG_PACKED_LITERAL(PACKED_INT32_ARRAY, int32_t)
			WG_PACKED_LITERAL(PACKED_INT64_ARRAY, int64_t)
			WG_PACKED_LITERAL(PACKED_FLOAT32_ARRAY, float)
			WG_PACKED_LITERAL(PACKED_FLOAT64_ARRAY, double)
			WG_PACKED_LITERAL(PACKED_STRING_ARRAY, String)
			WG_PACKED_LITERAL(PACKED_VECTOR2_ARRAY, Vector2)
			WG_PACKED_LITERAL(PACKED_VECTOR3_ARRAY, Vector3)
			WG_PACKED_LITERAL(PACKED_VECTOR4_ARRAY, Vector4)
			WG_PACKED_LITERAL(PACKED_COLOR_ARRAY, Color)
#undef WG_PACKED_LITERAL
			default:
				break;
		}
	}
	const bool dictionary = p_token.type == VP::TK_CURLY_BRACKET_OPEN;
	if (!p_type.concrete() || p_type.builtin != (dictionary ? Variant::DICTIONARY : Variant::ARRAY)) {
		r_error = "Container literal requires a fully declared destination type.";
		return ERR_PARSE_ERROR;
	}
	const Type &key_type = p_type.elements[0];
	const Ref<Script> key_script = element_script(key_type);
	const StringName key_native = key_script.is_valid() ? key_script->get_instance_base_type() : key_type.name;
	Variant decoded;
	VariantInternal::initialize(&decoded, p_type.builtin);
	Array *array = dictionary ? nullptr : VariantInternal::get_array(&decoded);
	Dictionary *map = dictionary ? VariantInternal::get_dictionary(&decoded) : nullptr;
	if (dictionary) {
		const Type &value_type = p_type.elements[1];
		const Ref<Script> value_script = element_script(value_type);
		map->set_typed(key_type.builtin, key_native, key_script, value_type.builtin, value_script.is_valid() ? value_script->get_instance_base_type() : value_type.name, value_script);
	} else {
		array->set_typed(key_type.builtin, key_native, key_script);
	}
	const VP::TokenType end = dictionary ? VP::TK_CURLY_BRACKET_CLOSE : VP::TK_BRACKET_CLOSE;
	RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
	while (p_token.type != end) {
		Variant item;
		RETURN_IF_ERROR(value(p_token, item, key_type, p_stream, r_line, r_error, p_resources, p_depth + 1));
		if (dictionary) {
			RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
			if (p_token.type != VP::TK_COLON) {
				r_error = "Expected ':' in dictionary literal.";
				return ERR_PARSE_ERROR;
			}
			RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
			Variant mapped;
			RETURN_IF_ERROR(value(p_token, mapped, p_type.elements[1], p_stream, r_line, r_error, p_resources, p_depth + 1));
			if (!map->set(item, mapped)) {
				r_error = "Dictionary entry does not match its declared types.";
				return ERR_PARSE_ERROR;
			}
		} else {
			const int64_t size = array->size();
			array->push_back(item);
			if (array->size() != size + 1) {
				r_error = "Array entry does not match its declared element type.";
				return ERR_PARSE_ERROR;
			}
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		if (p_token.type == end) {
			break;
		}
		if (p_token.type != VP::TK_COMMA) {
			r_error = "Expected a comma between container entries.";
			return ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
	}
	r_value = decoded;
	return OK;
}
