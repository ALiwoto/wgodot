// wgodot-changes::file
#pragma once

#include "wgodot_native_object.h"
#include "wgodot_native_warray.h"

#include "core/variant/wgodot_text_arguments.h"

#include <array>
#include <tuple>
#include <type_traits>

namespace WGodotNative {

template <class T>
struct TextFormat {
	static String format(const T &p_value, int p_depth, bool p_in_container) {
		return WGodotText::format_variant(Variant(p_value), p_depth, p_in_container);
	}
};

template <class T>
struct TextFormat<WArray<T>> {
	static String format(const WArray<T> &p_value, int p_depth, bool) {
		ERR_FAIL_COND_V_MSG(p_depth > MAX_RECURSION, "[...]", "Maximum array recursion reached!");
		// Select and retain this storage when formatting starts, as Variant's
		// Array conversion does. Earlier arguments may have replaced the slot.
		const WArray<T> values(p_value);
		String result("[");
		for (int64_t i = 0; i < values.size(); i++) {
			if (i) {
				result += ", ";
			}
			// Own this element while formatting: an object's _to_string may
			// resize the shared array. Do not retain a reference into its vector.
			const T element = values.get(i);
			result += TextFormat<T>::format(element, p_depth + 1, true);
		}
		return result + "]";
	}
};

// GDScript passes addresses of locals/fields, and temporaries for computed
// values. Preserve that distinction: borrow C++ lvalues and own rvalues.
// Create the view at the call site so copies/moves never leave pointers into
// a previous holder. Its lifetime is the enclosing full expression.
template <class... Args>
class TextArguments {
	using Values = std::tuple<Args...>;
	Values values;

	template <size_t... I>
	static String format_at(const Values &p_values, int p_index, std::index_sequence<I...>) {
		using Formatter = String (*)(const Values &);
		static constexpr std::array<Formatter, sizeof...(Args)> formatters{
			+[](const Values &p_args) { return TextFormat<std::decay_t<std::tuple_element_t<I, Values>>>::format(std::get<I>(p_args), 0, false); }...
		};
		return formatters[p_index](p_values);
	}

public:
	template <class... ValuesIn>
	explicit TextArguments(ValuesIn &&...p_args) : values(std::forward<ValuesIn>(p_args)...) {}
	operator WGodotText::Arguments() const {
		return WGodotText::Arguments(&values, sizeof...(Args), [](const void *p_values, int p_index) {
			return format_at(*static_cast<const Values *>(p_values), p_index, std::index_sequence_for<Args...>());
		});
	}
};

template <class T>
struct TextStorage {
	using Value = std::decay_t<T>;
	using Pointee = std::remove_pointer_t<Value>;
	// A raw self/singleton pointer needs the same lifetime/identity protection
	// as an Object argument held by the VM, including dynamically RefCounted ones.
	using Type = std::conditional_t<std::is_pointer_v<Value> && std::is_base_of_v<Object, Pointee>, ObjectValue<Pointee>, T>;
};

template <class... Args>
auto text_arguments(Args &&...p_args) {
	return TextArguments<typename TextStorage<Args>::Type...>(std::forward<Args>(p_args)...);
}

} // namespace WGodotNative
