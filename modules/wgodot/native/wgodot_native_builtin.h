// wgodot-changes::file
#pragma once

#include "wgodot_native_utility_traits.h"
#include "wgodot_native_values.h"

namespace WGodotNative {

template <class... Args>
inline constexpr bool same_numeric_arguments = false;

template <class First, class... Rest>
inline constexpr bool same_numeric_arguments<First, Rest...> =
		(std::is_same_v<First, int64_t> || std::is_same_v<First, double>) && (std::is_same_v<First, Rest> && ...);

// Preserve the original operand on ties and unordered (NaN) comparisons, as
// the generic utilities do. The engine's MIN/MAX/CLAMP macros differ here.
template <bool Maximum, class First, class... Rest>
First numeric_extreme(First p_value, const Rest &...p_rest) {
	auto accept = [&](First p_next) {
		if (Maximum ? p_value < p_next : p_value > p_next) {
			p_value = p_next;
		}
	};
	(accept(p_rest), ...);
	return p_value;
}

template <class Result, auto Function, class... Args>
Result invoke_checked(const char *p_name, const Args &...p_args) {
	if constexpr (numeric_utility<Function> == NumericUtility::CLAMP && sizeof...(Args) == 3 && same_numeric_arguments<Args...>) {
		return convert<Result>([](auto value, const auto &minimum, const auto &maximum) {
			if (value < minimum) {
				value = minimum;
			}
			if (value > maximum) {
				value = maximum;
			}
			return value;
		}(p_args...));
	} else {
		Callable::CallError error;
		error.error = Callable::CallError::CALL_OK;
		const auto result = Function(p_args..., error);
		ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), String("Invalid native utility arguments: ") + p_name);
		return convert<Result>(result);
	}
}

template <class Result, auto Function, class... Args>
Result invoke_vararg(const char *p_name, const Args &...p_args) {
	if constexpr (sizeof...(Args) >= 2 && same_numeric_arguments<Args...> &&
			(numeric_utility<Function> == NumericUtility::MINIMUM || numeric_utility<Function> == NumericUtility::MAXIMUM)) {
		return convert<Result>(numeric_extreme<numeric_utility<Function> == NumericUtility::MAXIMUM>(p_args...));
	} else {
		// Mixed numeric kinds can return either original type; keep that dynamic
		// result at the engine boundary, while still calling the function directly.
		Arguments<sizeof...(Args)> arguments(p_args...);
		Callable::CallError error;
		error.error = Callable::CallError::CALL_OK;
		const auto result = Function(arguments.data(), arguments.size(), error);
		ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), String("Invalid native variadic utility arguments: ") + p_name);
		return convert<Result>(result);
	}
}

// Taking the address inside this call also supports temporary receivers. Both
// the receiver and its arguments stay alive for the entire native invocation.
template <class Result, class Method, class Base, class... Args>
Result invoke_builtin(Method p_method, Base &&p_base, Args &&...p_args) {
	return invoke_member<Result>(p_method, &p_base, std::forward<Args>(p_args)...);
}

// Params are the constructor selected by the exporter from Godot's ordered
// registrations, including its numeric conversions before C++ overload selection.
template <class Result, class... Params, class... Args>
Result construct_builtin(Args &&...p_args) {
	static_assert(sizeof...(Params) == sizeof...(Args));
	return Result(static_cast<Params>(std::forward<Args>(p_args))...);
}

template <class Result>
Result construct_from_string(const String &p_value) {
	if constexpr (std::is_same_v<Result, int64_t>) {
		return p_value.to_int();
	} else {
		static_assert(std::is_same_v<Result, double>);
		return p_value.to_float();
	}
}

template <class T>
bool zero_divisor(const T &p_value) {
	if constexpr (std::is_arithmetic_v<T>) {
		return p_value == 0;
	} else {
		for (int i = 0; i < T::AXIS_COUNT; i++) {
			if (p_value[i] == 0) {
				return true;
			}
		}
		return false;
	}
}

template <class Left, class Right>
auto divide(const Left &p_left, const Right &p_right) {
	using Result = decltype(p_left / p_right);
	ERR_FAIL_COND_V_MSG(zero_divisor(p_right), Result(), "Division by zero error.");
	if constexpr (std::is_same_v<Left, int64_t> && std::is_same_v<Right, int64_t>) {
		return Math::division_no_overflow(p_left, p_right);
	} else {
		return p_left / p_right;
	}
}

template <class Left, class Right>
auto modulo(const Left &p_left, const Right &p_right) {
	using Result = decltype(p_left % p_right);
	ERR_FAIL_COND_V_MSG(zero_divisor(p_right), Result(), "Modulo by zero error.");
	if constexpr (std::is_same_v<Left, int64_t> && std::is_same_v<Right, int64_t>) {
		return Math::modulo_no_overflow(p_left, p_right);
	} else {
		return p_left % p_right;
	}
}

template <bool Left>
int64_t shift(int64_t p_value, int64_t p_count) {
	ERR_FAIL_COND_V_MSG(p_count < 0 || p_count >= 64, 0, "Invalid bit shift count.");
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_V_MSG(p_value < 0, 0, "Invalid operands for bit shifting. Only positive operands are supported.");
#endif
	if constexpr (Left) {
		return int64_t(uint64_t(p_value) << p_count);
	} else {
		return p_value >> p_count;
	}
}

} // namespace WGodotNative
