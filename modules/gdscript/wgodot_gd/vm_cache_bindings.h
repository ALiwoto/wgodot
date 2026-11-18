// wgodot-changes::file
#pragma once

#include "core/variant/array.h"

namespace WGodotVMCacheBindings {
enum Kind {
	OPERATOR,
	SETTER,
	GETTER,
	KEYED_SETTER,
	KEYED_GETTER,
	INDEXED_SETTER,
	INDEXED_GETTER,
	BUILTIN_METHOD,
	CONSTRUCTOR,
	UTILITY,
	GDSCRIPT_UTILITY,
	KIND_MAX,
};

// Addresses are lookup keys in memory only. The returned descriptor contains
// Variant types, member names and overload indices, never an address.
Array describe(Kind p_kind, uint64_t p_address);
uint64_t resolve(Kind p_kind, const Array &p_descriptor);
void clear();
} // namespace WGodotVMCacheBindings
