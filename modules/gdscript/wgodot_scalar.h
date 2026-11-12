// wgodot-changes::file

#pragma once

#include "core/variant/variant.h"
#include "core/math/wgodot_scalar.h"

// Scalar widths are language types, not new Variant tags. The source VM keeps
// ordinary numeric Variants; native scripts use the corresponding C++ storage.
namespace WGodotScalar {

Type from_name(const StringName &p_name);
Variant::Type base_type(Type p_type);
const char *name(Type p_type);
const char *cpp_type(Type p_type);
Variant narrow(const Variant &p_value, Type p_type);
Type operation_type(Type p_left, Type p_right, Variant::Type p_result, bool p_unary = false);

} // namespace WGodotScalar
