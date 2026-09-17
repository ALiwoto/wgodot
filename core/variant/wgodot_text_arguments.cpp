// wgodot-changes::file
#include "wgodot_text_arguments.h"

#ifdef TOOLS_ENABLED
#include "core/templates/hash_map.h"
#endif

// Defined by variant.cpp; keep container quoting in that single implementation.
String stringify_variant_clean(const Variant &p_variant, int p_recursion_count);

namespace WGodotText {

Arguments Arguments::from_variants(const Variant *const *p_values, int p_count) {
	return Arguments(p_values, p_count, [](const void *p_data, int p_index) {
		return static_cast<const Variant *const *>(p_data)[p_index]->stringify(0);
	});
}

String Arguments::join(const String &p_separator) const {
	String result;
	for (int i = 0; i < count; i++) {
		if (i) {
			result += p_separator;
		}
		result += formatter(values, i);
	}
	return result;
}

String format_variant(const Variant &p_value, int p_depth, bool p_in_container) {
	return p_in_container ? stringify_variant_clean(p_value, p_depth) : p_value.stringify(p_depth);
}

#ifdef TOOLS_ENABLED
static HashMap<StringName, String> utilities;

void register_utility_name(const StringName &p_name, const char *p_cpp_method) {
	utilities.insert(p_name, p_cpp_method);
}

const String *find_utility(const StringName &p_name) {
	return utilities.getptr(p_name);
}

void clear_utilities() {
	utilities.clear();
}
#endif

} // namespace WGodotText
