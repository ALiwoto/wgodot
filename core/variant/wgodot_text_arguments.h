// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

namespace WGodotText {

// A synchronous, non-owning view. Formatting is deferred until the consumer
// reads an argument, so disabled output never calls an object's _to_string.
class Arguments {
	const void *values;
	int count;
	String (*formatter)(const void *, int);

public:
	Arguments(const void *p_values, int p_count, String (*p_formatter)(const void *, int)) : values(p_values), count(p_count), formatter(p_formatter) {}
	static Arguments from_variants(const Variant *const *p_values, int p_count);
	int size() const { return count; }
	String join(const String &p_separator = String()) const;
};

// Reuse Godot's formatting, including quoting within containers and recursion
// limits. Native containers only supply traversal of their typed elements.
String format_variant(const Variant &p_value, int p_depth, bool p_in_container);

#ifdef TOOLS_ENABLED
void register_utility_name(const StringName &p_name, const char *p_cpp_method);
template <class R>
void register_utility(const StringName &p_name, const char *p_cpp_method, R (*)(const Arguments &)) {
	register_utility_name(p_name, p_cpp_method);
}
const String *find_utility(const StringName &p_name);
void clear_utilities();
#endif

} // namespace WGodotText
