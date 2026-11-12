// wgodot-changes::file
#pragma once

#include <cstdint>
#include <type_traits>

namespace WGodotScalar {

enum Type {
	NONE,
	FLOAT32,
	INT32,
	UINT8,
};

template <class T>
inline constexpr bool is_narrow = std::is_same_v<T, float> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint8_t>;

// Match the source VM's integer truncation and wrapping without signed overflow
// or implementation-defined conversion of an out-of-range unsigned integer.
template <class To, class From>
inline To convert(const From &p_value) {
	static_assert(is_narrow<To>);
	if constexpr (std::is_same_v<To, float>) {
		return static_cast<float>(static_cast<double>(p_value));
	} else if constexpr (std::is_same_v<To, int32_t>) {
		const int64_t bits = static_cast<int64_t>(p_value) & 0xffffffffLL;
		return static_cast<int32_t>(bits >= 0x80000000LL ? bits - 0x100000000LL : bits);
	} else {
		return static_cast<uint8_t>(static_cast<int64_t>(p_value) & 0xff);
	}
}

} // namespace WGodotScalar
