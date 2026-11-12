// wgodot-changes::file

#include "wgodot_scalar.h"

#include "core/math/wgodot_scalar.h"

namespace WGodotScalar {

Type from_name(const StringName &p_name) {
	if (p_name == SNAME("float32")) {
		return FLOAT32;
	}
	if (p_name == SNAME("int32")) {
		return INT32;
	}
	if (p_name == SNAME("uint8")) {
		return UINT8;
	}
	return NONE;
}

Variant::Type base_type(Type p_type) {
	return p_type == FLOAT32 ? Variant::FLOAT : Variant::INT;
}

const char *name(Type p_type) {
	switch (p_type) {
		case FLOAT32:
			return "float32";
		case INT32:
			return "int32";
		case UINT8:
			return "uint8";
		default:
			return "";
	}
}

const char *cpp_type(Type p_type) {
	switch (p_type) {
		case FLOAT32:
			return "float";
		case INT32:
			return "int32_t";
		case UINT8:
			return "uint8_t";
		default:
			return "";
	}
}

Variant narrow(const Variant &p_value, Type p_type) {
	switch (p_type) {
		case FLOAT32:
			return convert<float>(p_value);
		case INT32:
			return convert<int32_t>(p_value);
		case UINT8:
			return convert<uint8_t>(p_value);
		default:
			return p_value;
	}
}

Type operation_type(Type p_left, Type p_right, Variant::Type p_result, bool p_unary) {
	if (p_left != NONE && (p_unary || p_left == p_right) && base_type(p_left) == p_result) {
		return p_left;
	}
	// Mixing widths uses the existing wide numeric type. Explicit constructors
	// request narrower arithmetic and are constant-folded for literal operands.
	return NONE;
}

} // namespace WGodotScalar
