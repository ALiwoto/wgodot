// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

namespace WGodotNative {

// Native engine calls use the same Variant value semantics as GDScript, without
// parsing scripts or executing bytecode. Arguments stay on the C++ stack.
template <size_t Count>
class Arguments {
	std::array<Variant, Count> values;
	std::array<const Variant *, Count> pointers{};

public:
	template <class... Args>
	explicit Arguments(const Args &...p_args) : values{ Variant(p_args)... } {
		for (size_t i = 0; i < Count; i++) {
			pointers[i] = &values[i];
		}
	}
	const Variant **data() { return pointers.data(); }
	int size() const { return int(Count); }
};

template <class Result, bool WriteBack, class Base, class... Args>
Result builtin_call(Base &&p_base, const StringName &p_method, const Args &...p_args) {
	Arguments<sizeof...(Args)> arguments(p_args...);
	Variant base(p_base);
	Variant result;
	Callable::CallError error;
	base.callp(p_method, arguments.data(), arguments.size(), result, error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), Variant::get_call_error_text(p_method, arguments.data(), arguments.size(), error));
	if constexpr (WriteBack) {
		p_base = convert<std::decay_t<Base>>(base);
	}
	if constexpr (!std::is_void_v<Result>) {
		return convert<Result>(result);
	}
}

template <class Result, Variant::Type Type, class... Args>
Result construct(const Args &...p_args) {
	Arguments<sizeof...(Args)> arguments(p_args...);
	Variant result;
	Callable::CallError error;
	Variant::construct(Type, result, arguments.data(), arguments.size(), error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), "Invalid native game constructor: " + Variant::get_type_name(Type));
	return convert<Result>(result);
}

template <class Result, class... Args>
Result utility(const StringName &p_name, const Args &...p_args) {
	Arguments<sizeof...(Args)> arguments(p_args...);
	Variant result;
	Callable::CallError error;
	Variant::call_utility_function(p_name, &result, arguments.data(), arguments.size(), error);
	ERR_FAIL_COND_V_MSG(error.error != Callable::CallError::CALL_OK, Result(), Variant::get_call_error_text(p_name, arguments.data(), arguments.size(), error));
	if constexpr (!std::is_void_v<Result>) {
		return convert<Result>(result);
	}
}

template <class Result, class Base, class Key>
Result get_index(const Base &p_base, const Key &p_key) {
	bool valid;
	Variant result = Variant(p_base).get(p_key, &valid);
	ERR_FAIL_COND_V_MSG(!valid, Result(), "Invalid native game index.");
	return convert<Result>(result);
}

template <class Result, class T, class Key>
Result get_index(const WArray<T> &p_base, const Key &p_key) {
	return convert<Result>(p_base.get(p_key));
}

template <class T, class Key, class Value>
void set_index(WArray<T> &p_base, const Key &p_key, const Value &p_value) {
	p_base.set(p_key, convert<T>(p_value));
}

template <class Base, class Key, class Value>
void set_index(Base &p_base, const Key &p_key, const Value &p_value) {
	Variant base(p_base);
	bool valid;
	base.set(p_key, p_value, &valid);
	ERR_FAIL_COND_MSG(!valid, "Invalid native game index assignment.");
	p_base = convert<Base>(base);
}

template <class Result, class Base>
Result get_member(const Base &p_base, const StringName &p_name) {
	bool valid;
	Variant result = Variant(p_base).get_named(p_name, valid);
	ERR_FAIL_COND_V_MSG(!valid, Result(), "Invalid native game member: " + String(p_name));
	return convert<Result>(result);
}

template <class Base, class Value>
void set_member(Base &p_base, const StringName &p_name, const Value &p_value) {
	Variant base(p_base);
	bool valid;
	base.set_named(p_name, p_value, valid);
	ERR_FAIL_COND_MSG(!valid, "Invalid native game member assignment: " + String(p_name));
	p_base = convert<Base>(base);
}

template <class Result, class Left, class Right>
Result evaluate(Variant::Operator p_operation, const Left &p_left, const Right &p_right) {
	Variant result;
	bool valid;
	Variant::evaluate(p_operation, p_left, p_right, result, valid);
	ERR_FAIL_COND_V_MSG(!valid, Result(), "Invalid native game operator: " + Variant::get_operator_name(p_operation));
	return convert<Result>(result);
}

inline int64_t length(const Variant &p_value) {
	const Variant::Type type = p_value.get_type();
	if (type == Variant::DICTIONARY) {
		return VariantInternal::get_dictionary(&p_value)->size();
	}
	if (type == Variant::ARRAY) {
		return VariantInternal::get_array(&p_value)->size();
	}
	if (type == Variant::STRING_NAME) {
		return String(p_value).length();
	}
	if (type == Variant::STRING || (type >= Variant::PACKED_BYTE_ARRAY && type <= Variant::PACKED_VECTOR4_ARRAY)) {
		return int64_t(p_value.get_indexed_size());
	}
	ERR_FAIL_V_MSG(0, "Native game value cannot provide a length: " + Variant::get_type_name(type));
}

template <class T>
int64_t length(const WArray<T> &p_value) {
	return p_value.size();
}

// Own the array handle, not a buffer pointer: mutations may resize it, and
// assigning another array to the original variable must not redirect the loop.
template <class T>
class WArrayIterator {
	WArray<T> collection;
	int64_t position = 0;

public:
	explicit WArrayIterator(const WArray<T> &p_collection) : collection(p_collection) {}
	bool has_value() const { return position < collection.size(); }
	void next() { position++; }
	template <class Result>
	Result get() const { return convert<Result>(collection.get(position)); }
};

// Godot's iterator protocol preserves dictionary keys, integer ranges, and
// changes to shared collections made from inside a loop.
class Iterator {
	Variant collection;
	Variant position;
	bool valid = true;
	bool active = false;

public:
	explicit Iterator(const Variant &p_collection) : collection(p_collection) {
		active = collection.iter_init(position, valid);
		ERR_FAIL_COND_MSG(!valid, "Invalid native game iterator.");
	}
	bool has_value() const { return active && valid; }
	void next() {
		active = collection.iter_next(position, valid);
		ERR_FAIL_COND_MSG(!valid, "Invalid native game iterator advancement.");
	}
	template <class T>
	T get() {
		Variant value = collection.iter_get(position, valid);
		ERR_FAIL_COND_V_MSG(!valid, T(), "Invalid native game iterator value.");
		return convert<T>(value);
	}
};

class Range {
	int64_t position;
	int64_t end;
	int64_t step;

public:
	explicit Range(int64_t p_end) : Range(0, p_end, 1) {}
	Range(int64_t p_begin, int64_t p_end, int64_t p_step = 1) : position(p_begin), end(p_end), step(p_step) {
		ERR_FAIL_COND_MSG(step == 0, "Native game range step cannot be zero.");
	}
	bool has_value() const { return step > 0 ? position < end : step < 0 && position > end; }
	int64_t get() const { return position; }
	void next() { position += step; }
};

inline bool match_value(const Variant &p_value, const Variant &p_pattern) {
	return p_value.get_type() == p_pattern.get_type() && p_value == p_pattern;
}

} // namespace WGodotNative
