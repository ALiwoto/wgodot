// wgodot-changes::file
#pragma once

#include "core/variant/type_info.h"
#include "core/variant/variant_internal.h"

#include <type_traits>

namespace WGodotNative {

// The exporter supplies the canonical C++ type explicitly, so an engine enum
// or narrower numeric return type cannot change GDScript's matching rules.
template <class T>
bool match_typed_value(const Variant &p_value, const T &p_pattern) {
	if (p_value.get_type() == GetTypeInfo<T>::VARIANT_TYPE) {
		return VariantInternalAccessor<T>::get(&p_value) == p_pattern;
	}
	if constexpr (std::is_same_v<T, String>) {
		return p_value.get_type() == Variant::STRING_NAME && *VariantInternal::get_string_name(&p_value) == p_pattern;
	} else if constexpr (std::is_same_v<T, StringName>) {
		return p_value.get_type() == Variant::STRING && *VariantInternal::get_string(&p_value) == p_pattern;
	}
	return false;
}

inline bool match_value(const Variant &p_value, const Variant &p_pattern) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return p_pattern.get_type() == Variant::NIL;
		case Variant::BOOL:
			return match_typed_value<bool>(p_pattern, *VariantInternal::get_bool(&p_value));
		case Variant::INT:
			return match_typed_value<int64_t>(p_pattern, *VariantInternal::get_int(&p_value));
		case Variant::FLOAT:
			return match_typed_value<double>(p_pattern, *VariantInternal::get_float(&p_value));
		case Variant::STRING:
			return match_typed_value<String>(p_pattern, *VariantInternal::get_string(&p_value));
		case Variant::STRING_NAME:
			return match_typed_value<StringName>(p_pattern, *VariantInternal::get_string_name(&p_value));
		default:
			break;
	}
	if (p_value.get_type() != p_pattern.get_type()) {
		return false;
	}
	// Variant::operator== uses hash-key equality. Match uses the language's ==
	// operator, including its NaN and object-validity semantics.
	Variant result;
	bool valid = false;
	Variant::evaluate(Variant::OP_EQUAL, p_value, p_pattern, result, valid);
	return valid && bool(result);
}

} // namespace WGodotNative
