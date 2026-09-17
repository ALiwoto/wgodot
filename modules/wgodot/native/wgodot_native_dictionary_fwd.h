// wgodot-changes::file
#pragma once

#include <type_traits>

namespace WGodotNative {

template <class K, class V>
class WDictionary;

template <class T>
struct IsWDictionary : std::false_type {};
template <class K, class V>
struct IsWDictionary<WDictionary<K, V>> : std::true_type {};

} // namespace WGodotNative
