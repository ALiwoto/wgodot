// wgodot-changes::file
#pragma once

#include "core/string/string_name.h"

// Generated from WArray's C++ declarations. No runtime name lookup is emitted.
namespace WGodotCppArrayAPI {
enum class Type {
	BUILTIN,
	ELEMENT,
	ARRAY,
	OTHER_ARRAY,
	PREDICATE,
	COMPARATOR,
	MAPPER,
	REDUCER,
	ACCUMULATOR,
	MAPPED_ARRAY,
};
struct Method {
	const char *name;
	Type result;
	Type arguments[4];
	int required;
	int total;
	bool ordered;
	const char *copy_method;
	const char *unsupported;

	int callback_index() const {
		for (int i = 0; i < total; i++) {
			if (arguments[i] == Type::PREDICATE || arguments[i] == Type::COMPARATOR || arguments[i] == Type::MAPPER || arguments[i] == Type::REDUCER) {
				return i;
			}
		}
		return -1;
	}
};

#include "wgodot_cpp_array_api.gen.h"

inline const Method *find(const StringName &p_name, int p_arguments) {
	for (const Method &method : methods) {
		if (p_name == method.name && p_arguments >= method.required && p_arguments <= method.total) {
			return &method;
		}
	}
	return nullptr;
}
} // namespace WGodotCppArrayAPI
