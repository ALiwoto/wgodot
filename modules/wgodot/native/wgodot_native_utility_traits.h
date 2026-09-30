// wgodot-changes::file
#pragma once

#include "core/variant/variant_utility.h"

namespace WGodotNative {

// Shared by export-time type inference and the native implementations. These
// utilities return one of their operands, retaining its original numeric kind.
enum class NumericUtility {
	NONE,
	MINIMUM,
	MAXIMUM,
	CLAMP,
};

template <auto Function>
inline constexpr NumericUtility numeric_utility = NumericUtility::NONE;

template <>
inline constexpr NumericUtility numeric_utility<&VariantUtilityFunctions::min> = NumericUtility::MINIMUM;

template <>
inline constexpr NumericUtility numeric_utility<&VariantUtilityFunctions::max> = NumericUtility::MAXIMUM;

template <>
inline constexpr NumericUtility numeric_utility<&VariantUtilityFunctions::clamp> = NumericUtility::CLAMP;

} // namespace WGodotNative
