// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_value.cpp                                                */
/**************************************************************************/

#include "wgodot_debug_value.h"

#include "core/variant/variant_parser.h"

namespace WGodotDebugValue {

namespace {

bool get_array_size(const Variant &p_value, int64_t &r_size) {
	switch (p_value.get_type()) {
		case Variant::ARRAY:
			r_size = Array(p_value).size();
			return true;
		case Variant::PACKED_BYTE_ARRAY:
			r_size = PackedByteArray(p_value).size();
			return true;
		case Variant::PACKED_INT32_ARRAY:
			r_size = PackedInt32Array(p_value).size();
			return true;
		case Variant::PACKED_INT64_ARRAY:
			r_size = PackedInt64Array(p_value).size();
			return true;
		case Variant::PACKED_FLOAT32_ARRAY:
			r_size = PackedFloat32Array(p_value).size();
			return true;
		case Variant::PACKED_FLOAT64_ARRAY:
			r_size = PackedFloat64Array(p_value).size();
			return true;
		case Variant::PACKED_STRING_ARRAY:
			r_size = PackedStringArray(p_value).size();
			return true;
		case Variant::PACKED_VECTOR2_ARRAY:
			r_size = PackedVector2Array(p_value).size();
			return true;
		case Variant::PACKED_VECTOR3_ARRAY:
			r_size = PackedVector3Array(p_value).size();
			return true;
		case Variant::PACKED_COLOR_ARRAY:
			r_size = PackedColorArray(p_value).size();
			return true;
		case Variant::PACKED_VECTOR4_ARRAY:
			r_size = PackedVector4Array(p_value).size();
			return true;
		default:
			return false;
	}
}

} // namespace

bool format_compact(const Variant &p_value, String &r_text) {
	int64_t array_size = 0;
	if (get_array_size(p_value, array_size)) {
		r_text = vformat("%s(size=%d)", Variant::get_type_name(p_value.get_type()), array_size);
		return true;
	}
	if (p_value.get_type() == Variant::STRING) {
		const String string_value = p_value;
		if (string_value.length() > 128) {
			String preview;
			VariantWriter::write_to_string(string_value.substr(0, 64) + "...", preview);
			r_text = preview + vformat(" (+%d chars more)", string_value.length() - 64);
			return true;
		}
	}
	return false;
}

} // namespace WGodotDebugValue
