// wgodot-changes::file
#include "reflection.h"

#ifdef TOOLS_ENABLED
namespace WGodotSerialization {
Variant argument(const Dictionary &p_attributes, const StringName &p_name, const Variant &p_default, int p_index) {
	const Variant *value = p_attributes.getptr(p_name);
	if (!value) {
		return p_default;
	}
	const Array arguments = *value;
	return p_index < arguments.size() ? arguments[p_index] : p_default;
}

Vector<ReflectedField> reflected_fields(const Object *p_object, bool p_protobuf, bool p_json) {
	Ref<GDScript> script = p_object->get_script();
	Vector<ReflectedField> fields;
	if (script.is_null()) {
		return fields;
	}
	for (const auto &entry : script->wgodot_field_annotations) {
		ReflectedField field;
		field.name = entry.key;
		field.type = script->get_member_type(entry.key);
		field.id = argument(entry.value, p_protobuf ? SNAME("@PbFieldId") : SNAME("@BinaryFieldOrder"), -1);
		field.kind = kind_from_name(argument(entry.value, p_json ? SNAME("@JsonEncoding") : p_protobuf ? SNAME("@PbEncoding")
																									   : SNAME("@BinaryFieldType"),
				""));
		field.repeated = is_sequence(field.type.builtin_type, field.kind);
		field.key = kind_from_name(argument(entry.value, SNAME("@PbKeyEncoding"), ""));
		field.value = kind_from_name(argument(entry.value, SNAME("@PbValueEncoding"), ""));
		field.oneof = argument(entry.value, SNAME("@PbOneof"), "");
		field.packed = argument(entry.value, SNAME("@PbPacked"), true);
		field.json = argument(entry.value, SNAME("@JsonPropertyName"), String(entry.key));
		fields.push_back(field);
	}
	struct Order {
		bool operator()(const ReflectedField &p_left, const ReflectedField &p_right) const {
			return p_left.id == p_right.id ? p_left.name < p_right.name : p_left.id < p_right.id;
		}
	};
	fields.sort_custom<Order>();
	return fields;
}

bool reflected_big_endian(const Object *p_object) {
	Ref<GDScript> script = p_object->get_script();
	return script.is_valid() && String(argument(script->wgodot_class_annotations, SNAME("@BinaryOptions"), "little")) == "big";
}

bool reflected_strict(const Object *p_object) {
	Ref<GDScript> script = p_object->get_script();
	return script.is_null() || bool(argument(script->wgodot_class_annotations, SNAME("@BinaryOptions"), true, 1));
}

Variant default_value(const GDScriptDataType &p_type) {
	if (p_type.builtin_type == Variant::OBJECT) {
		return Variant();
	}
	Variant result;
	Callable::CallError error;
	Variant::construct(p_type.builtin_type, result, nullptr, 0, error);
	return result;
}

Variant new_model(const GDScriptDataType &p_type) {
	if (!p_type.script_type) {
		return Variant();
	}
	Callable::CallError error;
	Variant result = p_type.script_type->callp(SNAME("new"), nullptr, 0, error);
	return error.error == Callable::CallError::CALL_OK ? result : Variant();
}

void write_value(Writer &r_writer, const Variant &p_value, Kind p_kind, bool p_protobuf) {
	switch (p_kind) {
		case Kind::STRING: {
			const CharString string = String(p_value).utf8();
			write_bytes(r_writer, reinterpret_cast<const uint8_t *>(string.get_data()), string.length(), p_protobuf);
		} break;
		case Kind::BYTES: {
			const PackedByteArray bytes = p_value;
			write_bytes(r_writer, bytes.ptr(), bytes.size(), p_protobuf);
		} break;
		case Kind::MESSAGE: {
			Object *model = p_value;
			if (!p_protobuf) {
				r_writer.byte(model ? 1 : 0);
			}
			if (!model) {
				if (p_protobuf) {
					r_writer.varint(0);
				}
				return;
			}
			Writer nested;
			nested.depth = r_writer.depth + 1;
			if (nested.depth > 100) {
				r_writer.valid = false;
				return;
			}
			reflected_write(model, nested, p_protobuf);
			r_writer.valid &= nested.valid;
			write_bytes(r_writer, nested.data.ptr(), nested.data.size(), p_protobuf);
		} break;
		case Kind::VECTOR3: {
			const Vector3 vector = p_value;
			for (int axis = 0; axis < 3; ++axis) {
				write_number(r_writer, Kind::FLOAT, 0, vector[axis], false);
			}
		} break;
		case Kind::INVALID:
			r_writer.valid = false;
			break;
		default:
			write_number(r_writer, p_kind, int64_t(p_value), double(p_value), p_protobuf);
	}
}

bool read_value(Reader &r_reader, Variant &r_value, const GDScriptDataType &p_type, Kind p_kind, bool p_protobuf) {
	switch (p_kind) {
		case Kind::STRING: {
			String string;
			if (!read_string(r_reader, string, p_protobuf)) {
				return false;
			}
			r_value = string;
		} break;
		case Kind::BYTES: {
			PackedByteArray bytes;
			if (!read_bytes(r_reader, bytes, p_protobuf)) {
				return false;
			}
			r_value = bytes;
		} break;
		case Kind::MESSAGE: {
			if (!p_protobuf) {
				const uint64_t present = r_reader.fixed(1);
				if (!r_reader.valid || present > 1) {
					return false;
				}
				if (!present) {
					r_value = Variant();
					return true;
				}
			}
			const uint64_t length = p_protobuf ? r_reader.varint() : r_reader.fixed(4);
			Reader nested = r_reader.slice(length);
			if (!nested.valid) {
				return false;
			}
			Object *model = r_value;
			if (!model) {
				r_value = new_model(p_type);
				model = r_value;
			}
			if (!model || !reflected_read(model, nested, p_protobuf) || !nested.valid || nested.remaining()) {
				return false;
			}
			r_value = model;
		} break;
		case Kind::VECTOR3: {
			Vector3 vector;
			for (int axis = 0; axis < 3; ++axis) {
				int64_t unused = 0;
				double number = 0;
				if (!read_number(r_reader, Kind::FLOAT, unused, number, false)) {
					return false;
				}
				vector[axis] = real_t(number);
			}
			r_value = vector;
		} break;
		case Kind::INVALID:
			return false;
		default: {
			int64_t integer = 0;
			double number = 0;
			if (!read_number(r_reader, p_kind, integer, number, p_protobuf)) {
				return false;
			}
			if (p_kind == Kind::FLOAT || p_kind == Kind::DOUBLE) {
				r_value = number;
			} else if (p_kind == Kind::BOOL) {
				r_value = bool(integer);
			} else {
				r_value = integer;
			}
		} break;
	}
	return r_reader.valid;
}
} // namespace WGodotSerialization
#endif
