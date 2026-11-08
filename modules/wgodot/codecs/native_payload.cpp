// wgodot-changes::file
#include "native_payload.h"

namespace WGodotNativePayload {
String path(int64_t p_resource) {
	return "res://.wgodot/payload/" + itos(p_resource) + ".wgp";
}

String Value<String>::read(Input &p_input) {
	if (!p_input.available(4)) {
		return String();
	}
	const uint32_t length = p_input.file->get_32();
	if (!p_input.available(length)) {
		return String();
	}
	Vector<char> bytes;
	if (bytes.resize(uint64_t(length) + 1) != OK) {
		p_input.valid = false;
		return String();
	}
	p_input.bytes(bytes.ptrw(), length);
	bytes.ptrw()[length] = 0;
	return String::utf8(bytes.ptr(), length);
}

Reader::Reader(int64_t p_resource) {
	file = FileAccess::open(path(p_resource), FileAccess::READ);
	ERR_FAIL_COND_MSG(file.is_null(), "Native resource payload is missing: " + path(p_resource));
	const uint64_t length = file->get_length();
	ERR_FAIL_COND_MSG(length < 32 || file->get_32() != MAGIC || file->get_32() != VERSION || file->get_32() != sizeof(real_t), "Invalid native resource payload header.");
	const uint32_t count = file->get_32();
	const uint64_t index = file->get_64();
	ERR_FAIL_COND_MSG(index < 24 || index > length || (uint64_t(count) + 1) != (length - index) / 8 || (length - index) % 8, "Invalid native resource payload index.");
	ERR_FAIL_COND(offsets.resize(uint64_t(count) + 1) != OK);
	file->seek(index);
	uint64_t previous = 24;
	for (uint64_t i = 0; i <= count; i++) {
		const uint64_t offset = file->get_64();
		ERR_FAIL_COND_MSG(offset < previous || offset > index || (i < count && index - offset < 4), "Invalid native resource payload offset.");
		offsets.write[i] = offset;
		previous = offset;
	}
	ERR_FAIL_COND(offsets[count] != index || file->get_error() != OK);
	valid = true;
}

#ifdef TOOLS_ENABLED
Error Writer::open(const String &p_path) {
	offsets.clear();
	file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	RETURN_IF_ERROR(error);
	file->store_32(MAGIC);
	file->store_32(VERSION);
	file->store_32(sizeof(real_t));
	file->store_32(0);
	file->store_64(0);
	return file->get_error();
}

uint32_t Writer::append(const Variant &p_value) {
	ERR_FAIL_COND_V(error != OK || file.is_null(), 0);
	const uint32_t slot = offsets.size();
	offsets.push_back(file->get_position());
	file->store_32(p_value.get_type());
#define WG_PAYLOAD_CASE(m_enum, m_type) \
	case Variant::m_enum: \
		Value<m_type>::write(*file.ptr(), m_type(p_value)); \
		break;
	switch (p_value.get_type()) {
		WG_PAYLOAD_CASE(BOOL, bool)
		WG_PAYLOAD_CASE(INT, int64_t)
		WG_PAYLOAD_CASE(FLOAT, double)
		WG_PAYLOAD_CASE(STRING, String)
		WG_PAYLOAD_CASE(STRING_NAME, StringName)
		WG_PAYLOAD_CASE(NODE_PATH, NodePath)
		WG_PAYLOAD_CASE(VECTOR2, Vector2)
		WG_PAYLOAD_CASE(VECTOR2I, Vector2i)
		WG_PAYLOAD_CASE(RECT2, Rect2)
		WG_PAYLOAD_CASE(RECT2I, Rect2i)
		WG_PAYLOAD_CASE(VECTOR3, Vector3)
		WG_PAYLOAD_CASE(VECTOR3I, Vector3i)
		WG_PAYLOAD_CASE(TRANSFORM2D, Transform2D)
		WG_PAYLOAD_CASE(VECTOR4, Vector4)
		WG_PAYLOAD_CASE(VECTOR4I, Vector4i)
		WG_PAYLOAD_CASE(PLANE, Plane)
		WG_PAYLOAD_CASE(QUATERNION, Quaternion)
		WG_PAYLOAD_CASE(AABB, AABB)
		WG_PAYLOAD_CASE(BASIS, Basis)
		WG_PAYLOAD_CASE(TRANSFORM3D, Transform3D)
		WG_PAYLOAD_CASE(PROJECTION, Projection)
		WG_PAYLOAD_CASE(COLOR, Color)
		WG_PAYLOAD_CASE(PACKED_BYTE_ARRAY, PackedByteArray)
		WG_PAYLOAD_CASE(PACKED_INT32_ARRAY, PackedInt32Array)
		WG_PAYLOAD_CASE(PACKED_INT64_ARRAY, PackedInt64Array)
		WG_PAYLOAD_CASE(PACKED_FLOAT32_ARRAY, PackedFloat32Array)
		WG_PAYLOAD_CASE(PACKED_FLOAT64_ARRAY, PackedFloat64Array)
		WG_PAYLOAD_CASE(PACKED_STRING_ARRAY, PackedStringArray)
		WG_PAYLOAD_CASE(PACKED_VECTOR2_ARRAY, PackedVector2Array)
		WG_PAYLOAD_CASE(PACKED_VECTOR3_ARRAY, PackedVector3Array)
		WG_PAYLOAD_CASE(PACKED_VECTOR4_ARRAY, PackedVector4Array)
		WG_PAYLOAD_CASE(PACKED_COLOR_ARRAY, PackedColorArray)
		default:
			error = ERR_INVALID_DATA;
			ERR_PRINT("Unsupported native payload type: " + Variant::get_type_name(p_value.get_type()));
	}
#undef WG_PAYLOAD_CASE
	return slot;
}

uint32_t Writer::append_custom(uint32_t p_schema, const std::function<void(FileAccess &)> &p_write) {
	ERR_FAIL_COND_V(error != OK || file.is_null(), 0);
	const uint32_t slot = offsets.size();
	offsets.push_back(file->get_position());
	file->store_32(Variant::VARIANT_MAX);
	file->store_32(p_schema);
	p_write(*file.ptr());
	return slot;
}

Error Writer::finish() {
	RETURN_IF_ERROR(error);
	const uint64_t index = file->get_position();
	offsets.push_back(index);
	for (uint64_t offset : offsets) {
		file->store_64(offset);
	}
	file->seek(12);
	file->store_32(offsets.size() - 1);
	file->store_64(index);
	file->flush();
	error = file->get_error();
	file.unref();
	return error;
}
#endif
} // namespace WGodotNativePayload
