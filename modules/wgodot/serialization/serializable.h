// wgodot-changes::file
#pragma once

#include "wire.h"

#include "core/object/ref_counted.h"
#include "core/object/wgodot_interface.h"

namespace WGodotSerialization {
template <class T, auto Decode, class Data>
Ref<T> deserialize(const Data &p_data) {
	Ref<T> model = T::create();
	if (!(model.ptr()->*Decode)(p_data)) {
		return {};
	}
	return model;
}
} // namespace WGodotSerialization

class JsonSerializable {
	WGD_INTERFACE(JsonSerializable);

public:
	virtual String serialize_json() const;
	virtual bool _deserialize_json(const String &p_data);
	virtual Dictionary serialize_json_dict() const;
	virtual bool _deserialize_json_dict(const Dictionary &p_data);
	virtual Dictionary write_json() const = 0;
	virtual bool read_json(const Dictionary &p_data) = 0;
	template <class T>
	static Ref<T> deserialize_json(const String &p_data) {
		return WGodotSerialization::deserialize<T, &JsonSerializable::_deserialize_json>(p_data);
	}
	template <class T>
	static Ref<T> deserialize_json_dict(const Dictionary &p_data) {
		return WGodotSerialization::deserialize<T, &JsonSerializable::_deserialize_json_dict>(p_data);
	}
};

class BinarySerializable {
	WGD_INTERFACE(BinarySerializable);

public:
	virtual PackedByteArray serialize_binary() const;
	virtual bool _deserialize_binary(const PackedByteArray &p_data);
	virtual void write_binary(WGodotSerialization::Writer &r_writer) const = 0;
	virtual bool read_binary(WGodotSerialization::Reader &r_reader) = 0;
	template <class T>
	static Ref<T> deserialize_binary(const PackedByteArray &p_data) {
		return WGodotSerialization::deserialize<T, &BinarySerializable::_deserialize_binary>(p_data);
	}
};

class ProtobufSerializable {
	WGD_INTERFACE(ProtobufSerializable);

public:
	virtual PackedByteArray serialize_protobuf() const;
	virtual bool _deserialize_protobuf(const PackedByteArray &p_data);
	virtual void write_protobuf(WGodotSerialization::Writer &r_writer) const = 0;
	virtual bool read_protobuf(WGodotSerialization::Reader &r_reader) = 0;
	virtual bool has_field(uint32_t p_id) const;
	virtual void set_field_present(uint32_t p_id, bool p_present = true);
	virtual void clear_field_presence();
	virtual bool has_protobuf_field(uint32_t p_id) const = 0;
	virtual void set_protobuf_field(uint32_t p_id, bool p_present) = 0;
	virtual void clear_protobuf_fields() = 0;
	template <class T>
	static Ref<T> deserialize_protobuf(const PackedByteArray &p_data) {
		return WGodotSerialization::deserialize<T, &ProtobufSerializable::_deserialize_protobuf>(p_data);
	}
};
