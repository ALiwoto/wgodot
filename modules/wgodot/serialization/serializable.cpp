// wgodot-changes::file
#include "serializable.h"

#include "core/io/json.h"

String JsonSerializable::serialize_json() const {
	return JSON::stringify(serialize_json_dict(), "", true, true);
}

bool JsonSerializable::_deserialize_json(const String &p_data) {
	JSON parser;
	if (parser.parse_preserving_integers(p_data) != OK || parser.get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}
	return _deserialize_json_dict(parser.get_data());
}

Dictionary JsonSerializable::serialize_json_dict() const {
	return write_json();
}

bool JsonSerializable::_deserialize_json_dict(const Dictionary &p_data) {
	return read_json(p_data);
}

PackedByteArray BinarySerializable::serialize_binary() const {
	WGodotSerialization::Writer writer;
	write_binary(writer);
	return writer.valid ? writer.data : PackedByteArray();
}

bool BinarySerializable::_deserialize_binary(const PackedByteArray &p_data) {
	WGodotSerialization::Reader reader;
	reader.data = p_data.ptr();
	reader.size = p_data.size();
	return read_binary(reader) && reader.valid && reader.remaining() == 0;
}

PackedByteArray ProtobufSerializable::serialize_protobuf() const {
	WGodotSerialization::Writer writer;
	write_protobuf(writer);
	return writer.valid ? writer.data : PackedByteArray();
}

bool ProtobufSerializable::_deserialize_protobuf(const PackedByteArray &p_data) {
	WGodotSerialization::Reader reader;
	reader.data = p_data.ptr();
	reader.size = p_data.size();
	return read_protobuf(reader) && reader.valid && reader.remaining() == 0;
}

bool ProtobufSerializable::has_field(uint32_t p_id) const {
	return has_protobuf_field(p_id);
}

void ProtobufSerializable::set_field_present(uint32_t p_id, bool p_present) {
	set_protobuf_field(p_id, p_present);
}

void ProtobufSerializable::clear_field_presence() {
	clear_protobuf_fields();
}
