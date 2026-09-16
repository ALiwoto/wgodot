// wgodot-changes::file
#pragma once

#include "core/object/callable_mp.h"

namespace WGodotNative {

// Static game methods have no Script resource at runtime. The function pointer
// supplies stable Callable identity; Godot's binder handles optional arguments.
template <class Return, class... Params>
class StaticCallable : public CallableCustomMethodPointerBase {
	Return (*method)(Params...);
	StringName name;
	Vector<Variant> defaults;

public:
	StaticCallable(Return (*p_method)(Params...), const StringName &p_name, const Vector<Variant> &p_defaults) : method(p_method), name(p_name), defaults(p_defaults) {
		_setup(reinterpret_cast<uint32_t *>(&method), sizeof(method));
	}
	bool is_valid() const override { return true; }
	ObjectID get_object() const override { return ObjectID(); }
	StringName get_method() const override { return name; }
	String get_as_text() const override { return name; }
	int get_argument_count(bool &r_valid) const override {
		r_valid = true;
		return sizeof...(Params);
	}
	void call(const Variant **p_arguments, int p_count, Variant &r_result, Callable::CallError &r_error) const override {
		if constexpr (std::is_void_v<Return>) {
			call_with_variant_args_static_dv(method, p_arguments, p_count, r_error, defaults);
		} else {
			call_with_variant_args_static_ret_dv(method, p_arguments, p_count, r_result, r_error, defaults);
		}
	}
};

template <class Return, class... Params>
Callable static_callable(Return (*p_method)(Params...), const StringName &p_name, const Vector<Variant> &p_defaults) {
	using Implementation = StaticCallable<Return, Params...>;
	return Callable(memnew(Implementation(p_method, p_name, p_defaults)));
}

} // namespace WGodotNative
