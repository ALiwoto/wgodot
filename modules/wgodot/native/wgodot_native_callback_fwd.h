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

template <class... Args>
class WSignal;
template <class T>
struct IsWSignal : std::false_type {};
template <class... Args>
struct IsWSignal<WSignal<Args...>> : std::true_type {};
} // namespace WGodotNative
