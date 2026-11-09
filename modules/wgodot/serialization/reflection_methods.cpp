// wgodot-changes::file
#include "reflection.h"

#ifdef TOOLS_ENABLED
#include "core/io/json.h"
#include "core/object/wgodot_interface_registry.h"
#include "core/variant/binder_common.h"

using namespace WGodotSerialization;

namespace {
Variant decode_factory(Object *p_class, const StringName &p_decoder, const Variant **p_args, int p_count, Callable::CallError &r_error) {
	Variant model = p_class->callp(SNAME("new"), nullptr, 0, r_error);
	if (r_error.error != Callable::CallError::CALL_OK) {
		return Variant();
	}
	Object *object = model;
	if (!object) {
		r_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}
	const Variant decoded = object->callp(p_decoder, p_args, p_count, r_error);
	return r_error.error == Callable::CallError::CALL_OK && bool(decoded) ? model : Variant();
}

template <auto Function>
struct ReflectedMethod;

template <class R, class... Args, R (*Function)(Object *, Args...)>
struct ReflectedMethod<Function> {
	static Variant call(Object *p_object, const Variant **p_args, int p_count, Callable::CallError &r_error) {
		Variant result;
		if constexpr (std::is_void_v<R>) {
			call_with_variant_args_static_helper_dv(p_object, Function, p_args, p_count, {}, r_error);
		} else {
			call_with_variant_args_retc_static_helper_dv(p_object, Function, p_args, p_count, result, {}, r_error);
		}
		return result;
	}
};

template <bool Protobuf>
PackedByteArray encode(Object *p_object) {
	Writer writer;
	reflected_write(p_object, writer, Protobuf);
	return writer.valid ? writer.data : PackedByteArray();
}

template <bool Protobuf>
bool decode(Object *p_object, const PackedByteArray &p_data) {
	Reader reader;
	reader.data = p_data.ptr();
	reader.size = p_data.size();
	return reflected_read(p_object, reader, Protobuf) && reader.valid && !reader.remaining();
}

Dictionary encode_json_dict(Object *p_object) {
	return reflected_json(p_object);
}

String encode_json(Object *p_object) {
	return JSON::stringify(p_object->call(SNAME("serialize_json_dict")), "", true, true);
}

bool decode_json(Object *p_object, const String &p_data) {
	JSON json;
	return json.parse_preserving_integers(p_data) == OK && json.get_data().get_type() == Variant::DICTIONARY && bool(p_object->call(SNAME("_deserialize_json_dict"), json.get_data()));
}

bool has_present_field(Object *p_object, uint32_t p_id) {
	return reflected_has_field(p_object, p_id);
}

Variant set_field(Object *p_object, const Variant **p_args, int p_count, Callable::CallError &r_error) {
	call_with_variant_args_static_helper_dv(p_object, &reflected_set_field, p_args, p_count, { true }, r_error);
	return Variant();
}
} // namespace

bool WGodotSerialization::reflected_has_field(const Object *p_object, uint32_t p_id) {
	const PackedInt32Array fields = p_object->get_meta(SNAME("_protobuf_fields"), PackedInt32Array());
	return fields.has(p_id);
}

void WGodotSerialization::reflected_set_field(Object *p_object, uint32_t p_id, bool p_present) {
	bool known = false;
	for (const auto &field : reflected_fields(p_object, true)) {
		if (field.id == int64_t(p_id) && field.type.builtin_type != Variant::ARRAY && field.type.builtin_type != Variant::DICTIONARY) {
			known = true;
			break;
		}
	}
	if (!known) {
		return;
	}
	PackedInt32Array fields = p_object->get_meta(SNAME("_protobuf_fields"), PackedInt32Array());
	const int64_t index = fields.find(p_id);
	if (p_present && index < 0) {
		fields.push_back(p_id);
	} else if (!p_present && index >= 0) {
		fields.remove_at(index);
	}
	if (fields.is_empty()) {
		p_object->remove_meta(SNAME("_protobuf_fields"));
	} else {
		p_object->set_meta(SNAME("_protobuf_fields"), fields);
	}
}

void WGodotSerialization::reflected_clear_fields(Object *p_object) {
	p_object->remove_meta(SNAME("_protobuf_fields"));
}

void JsonSerializable::_bind_interface(WGodotNativeInterfaces::Builder &p_builder) {
	p_builder.factory_method<String>("deserialize_json", [](Object *p_class, const Variant **p_args, int p_count, Callable::CallError &r_error) {
		return decode_factory(p_class, SNAME("_deserialize_json"), p_args, p_count, r_error);
	});
	p_builder.factory_method<Dictionary>("deserialize_json_dict", [](Object *p_class, const Variant **p_args, int p_count, Callable::CallError &r_error) {
		return decode_factory(p_class, SNAME("_deserialize_json_dict"), p_args, p_count, r_error);
	});
	p_builder.default_method("serialize_json", &JsonSerializable::serialize_json, ReflectedMethod<&encode_json>::call);
	p_builder.default_method("serialize_json_dict", &JsonSerializable::serialize_json_dict, ReflectedMethod<&encode_json_dict>::call);
	p_builder.default_method("_deserialize_json", &JsonSerializable::_deserialize_json, ReflectedMethod<&decode_json>::call);
	p_builder.default_method("_deserialize_json_dict", &JsonSerializable::_deserialize_json_dict, ReflectedMethod<&reflected_parse_json>::call);
}

void BinarySerializable::_bind_interface(WGodotNativeInterfaces::Builder &p_builder) {
	p_builder.factory_method<PackedByteArray>("deserialize_binary", [](Object *p_class, const Variant **p_args, int p_count, Callable::CallError &r_error) {
		return decode_factory(p_class, SNAME("_deserialize_binary"), p_args, p_count, r_error);
	});
	p_builder.default_method("serialize_binary", &BinarySerializable::serialize_binary, ReflectedMethod<&encode<false>>::call);
	p_builder.default_method("_deserialize_binary", &BinarySerializable::_deserialize_binary, ReflectedMethod<&decode<false>>::call);
}

void ProtobufSerializable::_bind_interface(WGodotNativeInterfaces::Builder &p_builder) {
	p_builder.factory_method<PackedByteArray>("deserialize_protobuf", [](Object *p_class, const Variant **p_args, int p_count, Callable::CallError &r_error) {
		return decode_factory(p_class, SNAME("_deserialize_protobuf"), p_args, p_count, r_error);
	});
	p_builder.default_method("serialize_protobuf", &ProtobufSerializable::serialize_protobuf, ReflectedMethod<&encode<true>>::call);
	p_builder.default_method("_deserialize_protobuf", &ProtobufSerializable::_deserialize_protobuf, ReflectedMethod<&decode<true>>::call);
	p_builder.default_method("has_field", &ProtobufSerializable::has_field, ReflectedMethod<&has_present_field>::call);
	p_builder.default_method("set_field_present", &ProtobufSerializable::set_field_present, set_field);
	p_builder.default_arguments("set_field_present", { true });
	p_builder.default_method("clear_field_presence", &ProtobufSerializable::clear_field_presence, ReflectedMethod<&reflected_clear_fields>::call);
}
#endif
