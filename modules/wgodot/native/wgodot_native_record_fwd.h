// wgodot-changes::file
#pragma once

#include <type_traits>

namespace WGodotNative {
struct RecordTag {};
template <class T>
inline constexpr bool is_record = std::is_base_of_v<RecordTag, T>;
} // namespace WGodotNative
