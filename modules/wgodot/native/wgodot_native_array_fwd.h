// wgodot-changes::file
#pragma once

#include <type_traits>

namespace WGodotNative {
template <class T>
class WArray;
template <class T>
struct IsWArray : std::false_type {};
template <class T>
struct IsWArray<WArray<T>> : std::true_type {};
} // namespace WGodotNative
