// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

#include <type_traits>

namespace WGodotNative {
template <class T, Variant::Type Kind = Variant::ARRAY>
class WArray;
template <class T>
struct IsWArray : std::false_type {};
template <class T>
struct IsWArray<WArray<T>> : std::true_type {};

template <class T>
struct IsPacked : std::false_type {};

template <class T, Variant::Type Kind>
struct IsPacked<WArray<T, Kind>> : std::bool_constant<Kind != Variant::ARRAY> {};
} // namespace WGodotNative
