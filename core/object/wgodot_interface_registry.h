// wgodot-changes::file
#pragma once

#include "core/object/method_info.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/type_info.h"

#include <type_traits>

namespace WGodotNativeInterfaces {

struct Property {
	PropertyInfo info;
	StringName getter;
	StringName setter;
};

struct Descriptor {
	StringName name;
	StringName native_base;
	String cpp_type;
	String cpp_header;
	Vector<StringName> parents;
	HashMap<StringName, MethodInfo> methods;
	HashMap<StringName, Property> properties;
	HashMap<StringName, MethodInfo> signals;
	HashMap<StringName, HashMap<StringName, int64_t>> enums;
	HashMap<StringName, int64_t> constants;
};

class Builder {
	Descriptor &descriptor;

public:
	explicit Builder(Descriptor &p_descriptor) : descriptor(p_descriptor) {}
	void import_api(const StringName &p_class);
	void inherits(const StringName &p_interface);
	void method(const MethodInfo &p_method) { descriptor.methods.insert(p_method.name, p_method); }
	void property(const PropertyInfo &p_property, const StringName &p_getter, const StringName &p_setter = StringName()) {
		descriptor.properties.insert(p_property.name, { p_property, p_getter, p_setter });
	}
	void signal(const MethodInfo &p_signal) { descriptor.signals.insert(p_signal.name, p_signal); }
	void constant(const StringName &p_name, int64_t p_value, const StringName &p_enum = StringName()) {
		descriptor.constants.insert(p_name, p_value);
		if (!p_enum.is_empty()) {
			descriptor.enums[p_enum].insert(p_name, p_value);
		}
	}
	template <class C, class R, class... Args>
	void method(const StringName &p_name, R (C::*)(Args...)) {
		MethodInfo info;
		info.name = p_name;
		if constexpr (!std::is_void_v<R>) {
			info.return_val = GetTypeInfo<std::decay_t<R>>::get_class_info();
		}
		(info.arguments.push_back(GetTypeInfo<std::decay_t<Args>>::get_class_info()), ...);
		method(info);
	}
	template <class C, class R, class... Args>
	void method(const StringName &p_name, R (C::*)(Args...) const) {
		MethodInfo info;
		info.name = p_name;
		info.flags |= METHOD_FLAG_CONST;
		if constexpr (!std::is_void_v<R>) {
			info.return_val = GetTypeInfo<std::decay_t<R>>::get_class_info();
		}
		(info.arguments.push_back(GetTypeInfo<std::decay_t<Args>>::get_class_info()), ...);
		method(info);
	}
};

void register_descriptor(const Descriptor &p_descriptor);
const Descriptor *get_descriptor(const StringName &p_name);
void get_descriptor_list(LocalVector<StringName> &r_names);
void clear_descriptors();

template <class T>
void register_interface(const StringName &p_base, const String &p_header) {
	Descriptor descriptor;
	descriptor.name = T::wgodot_interface_name();
	descriptor.native_base = p_base;
	descriptor.cpp_type = "::" + String(descriptor.name);
	descriptor.cpp_header = p_header;
	Builder builder(descriptor);
	T::_bind_interface(builder);
	register_descriptor(descriptor);
}

} // namespace WGodotNativeInterfaces

#define WGDREGISTER_INTERFACE(m_type, m_base, m_header) \
	WGodotNativeInterfaces::register_interface<m_type>(m_base, m_header)
