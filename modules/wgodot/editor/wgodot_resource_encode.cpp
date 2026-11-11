// wgodot-changes::file
#include "wgodot_resource_encode.h"

namespace WGodotResourceEncode {
using namespace WGodotResourceProtobuf;
using Type = GDScriptParser::DataType;
using Path = std::function<int64_t(const String &)>;

namespace {
template <class T>
Error typed_array(Writer &r_writer, const Array &p_array, Context &p_context) {
	Vector<T> values;
	RETURN_IF_ERROR(values.resize(p_array.size()));
	T *destination = values.ptrw();
	for (int i = 0; i < p_array.size(); i++) {
		destination[i] = p_array[i];
	}
	Value<Vector<T>>::write(r_writer, values, p_context);
	return r_writer.valid ? OK : ERR_INVALID_DATA;
}

int wire(const Type &p_type) {
	if (p_type.wgodot_resource_path || p_type.kind == Type::ENUM || p_type.builtin_type == Variant::BOOL || p_type.builtin_type == Variant::INT) {
		return 0;
	}
	return p_type.builtin_type == Variant::FLOAT ? 1 : 2;
}

Error value(Writer &r_writer, const Variant &p_value, const Type &p_type, Context &p_context, const Path &p_path) {
	if (p_type.wgodot_resource_path) {
		Value<int64_t>::write(r_writer, p_path(p_value), p_context);
		return OK;
	}
	if (p_type.kind == Type::ENUM) {
		Value<int64_t>::write(r_writer, int64_t(p_value), p_context);
		return OK;
	}
	if (p_type.builtin_type == Variant::ARRAY) {
		if (!p_type.has_container_element_type(0)) {
			return ERR_INVALID_DATA;
		}
		const Type &element = p_type.get_container_element_type(0);
		const Array array = p_value;
		switch (element.wgodot_resource_path ? Variant::NIL : element.builtin_type) {
#define WG_RESOURCE_TYPED_ARRAY(m_kind, m_type) \
	case Variant::m_kind: \
		return typed_array<m_type>(r_writer, array, p_context);
			WG_RESOURCE_TYPED_ARRAY(BOOL, bool)
			WG_RESOURCE_TYPED_ARRAY(INT, int64_t)
			WG_RESOURCE_TYPED_ARRAY(FLOAT, double)
			WG_RESOURCE_TYPED_ARRAY(STRING, String)
			WG_RESOURCE_TYPED_ARRAY(STRING_NAME, StringName)
			WG_RESOURCE_TYPED_ARRAY(NODE_PATH, NodePath)
			WG_RESOURCE_TYPED_ARRAY(VECTOR2, Vector2)
			WG_RESOURCE_TYPED_ARRAY(VECTOR2I, Vector2i)
			WG_RESOURCE_TYPED_ARRAY(RECT2, Rect2)
			WG_RESOURCE_TYPED_ARRAY(RECT2I, Rect2i)
			WG_RESOURCE_TYPED_ARRAY(VECTOR3, Vector3)
			WG_RESOURCE_TYPED_ARRAY(VECTOR3I, Vector3i)
			WG_RESOURCE_TYPED_ARRAY(TRANSFORM2D, Transform2D)
			WG_RESOURCE_TYPED_ARRAY(VECTOR4, Vector4)
			WG_RESOURCE_TYPED_ARRAY(VECTOR4I, Vector4i)
			WG_RESOURCE_TYPED_ARRAY(PLANE, Plane)
			WG_RESOURCE_TYPED_ARRAY(QUATERNION, Quaternion)
			WG_RESOURCE_TYPED_ARRAY(AABB, AABB)
			WG_RESOURCE_TYPED_ARRAY(BASIS, Basis)
			WG_RESOURCE_TYPED_ARRAY(TRANSFORM3D, Transform3D)
			WG_RESOURCE_TYPED_ARRAY(PROJECTION, Projection)
			WG_RESOURCE_TYPED_ARRAY(COLOR, Color)
#undef WG_RESOURCE_TYPED_ARRAY
			default:
				break;
		}
		Writer contents;
		Writer first;
		bool repeated = array.size() > 4;
		if (repeated) {
			RETURN_IF_ERROR(value(first, array[0], element, p_context, p_path));
			for (int i = 1; repeated && i < array.size(); i++) {
				Writer next;
				RETURN_IF_ERROR(value(next, array[i], element, p_context, p_path));
				repeated = first.valid && next.valid && first.data == next.data;
			}
		}
		if (repeated) {
			contents.tag(2, wire(element));
			contents.raw(first.data.ptr(), first.data.size());
			contents.tag(3, 0);
			contents.varint(array.size());
		} else if (wire(element) != 2) {
			Writer packed;
			for (const Variant &item : array) {
				RETURN_IF_ERROR(value(packed, item, element, p_context, p_path));
			}
			contents.tag(1, 2);
			message(contents, packed);
		} else {
			for (const Variant &item : array) {
				RETURN_IF_ERROR(field(contents, 1, item, element, p_context, p_path));
			}
		}
		message(r_writer, contents);
		return r_writer.valid ? OK : ERR_INVALID_DATA;
	}
	if (p_type.builtin_type == Variant::DICTIONARY) {
		if (!p_type.has_container_element_type(0) || !p_type.has_container_element_type(1)) {
			return ERR_INVALID_DATA;
		}
		const Dictionary dictionary = p_value;
		Writer contents;
		for (const auto &item : dictionary) {
			Writer entry;
			RETURN_IF_ERROR(field(entry, 1, item.key, p_type.get_container_element_type(0), p_context, p_path));
			RETURN_IF_ERROR(field(entry, 2, item.value, p_type.get_container_element_type(1), p_context, p_path));
			contents.tag(1, 2);
			message(contents, entry);
		}
		message(r_writer, contents);
		return r_writer.valid ? OK : ERR_INVALID_DATA;
	}
	// All conversions below are guarded by the source type checker. No Variant
	// tags, property names, or reflection instructions enter the resource pack.
	switch (p_type.builtin_type) {
#define WG_RESOURCE_VALUE(m_kind, m_type) \
	case Variant::m_kind: { \
		const m_type typed = p_value; \
		Value<m_type>::write(r_writer, typed, p_context); \
	} break;
		WG_RESOURCE_VALUE(BOOL, bool)
		WG_RESOURCE_VALUE(INT, int64_t)
		WG_RESOURCE_VALUE(FLOAT, double)
		WG_RESOURCE_VALUE(STRING, String)
		WG_RESOURCE_VALUE(STRING_NAME, StringName)
		WG_RESOURCE_VALUE(NODE_PATH, NodePath)
		WG_RESOURCE_VALUE(VECTOR2, Vector2)
		WG_RESOURCE_VALUE(VECTOR2I, Vector2i)
		WG_RESOURCE_VALUE(RECT2, Rect2)
		WG_RESOURCE_VALUE(RECT2I, Rect2i)
		WG_RESOURCE_VALUE(VECTOR3, Vector3)
		WG_RESOURCE_VALUE(VECTOR3I, Vector3i)
		WG_RESOURCE_VALUE(TRANSFORM2D, Transform2D)
		WG_RESOURCE_VALUE(VECTOR4, Vector4)
		WG_RESOURCE_VALUE(VECTOR4I, Vector4i)
		WG_RESOURCE_VALUE(PLANE, Plane)
		WG_RESOURCE_VALUE(QUATERNION, Quaternion)
		WG_RESOURCE_VALUE(AABB, AABB)
		WG_RESOURCE_VALUE(BASIS, Basis)
		WG_RESOURCE_VALUE(TRANSFORM3D, Transform3D)
		WG_RESOURCE_VALUE(PROJECTION, Projection)
		WG_RESOURCE_VALUE(COLOR, Color)
		WG_RESOURCE_VALUE(OBJECT, Ref<Resource>)
		WG_RESOURCE_VALUE(PACKED_BYTE_ARRAY, PackedByteArray)
		WG_RESOURCE_VALUE(PACKED_INT32_ARRAY, PackedInt32Array)
		WG_RESOURCE_VALUE(PACKED_INT64_ARRAY, PackedInt64Array)
		WG_RESOURCE_VALUE(PACKED_FLOAT32_ARRAY, PackedFloat32Array)
		WG_RESOURCE_VALUE(PACKED_FLOAT64_ARRAY, PackedFloat64Array)
		WG_RESOURCE_VALUE(PACKED_STRING_ARRAY, PackedStringArray)
		WG_RESOURCE_VALUE(PACKED_VECTOR2_ARRAY, PackedVector2Array)
		WG_RESOURCE_VALUE(PACKED_VECTOR3_ARRAY, PackedVector3Array)
		WG_RESOURCE_VALUE(PACKED_VECTOR4_ARRAY, PackedVector4Array)
		WG_RESOURCE_VALUE(PACKED_COLOR_ARRAY, PackedColorArray)
#undef WG_RESOURCE_VALUE
		default:
			return ERR_INVALID_DATA;
	}
	return r_writer.valid ? OK : ERR_INVALID_DATA;
}
} //namespace

Error field(Writer &r_writer, uint32_t p_id, const Variant &p_value, const Type &p_type, Context &p_context, const Path &p_path) {
	if (p_type.is_variant() || !p_type.is_hard_type()) {
		return ERR_INVALID_DATA;
	}
	r_writer.tag(p_id, wire(p_type));
	return value(r_writer, p_value, p_type, p_context, p_path);
}
} //namespace WGodotResourceEncode
