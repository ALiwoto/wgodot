// wgodot-changes::file
#pragma once

#include <type_traits>

namespace WGodotNative {
template <class Signature>
class WCallable;
template <class T>
struct IsWCallable : std::false_type {};
template <class Signature>
struct IsWCallable<WCallable<Signature>> : std::true_type {};
} // namespace WGodotNative
