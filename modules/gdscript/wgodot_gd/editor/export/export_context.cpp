// wgodot-changes::file
/**************************************************************************/
/*  export_context.cpp                                                    */
/**************************************************************************/

#include "export_context.h"

#include "export_strings.h"

namespace WGodotGDScriptExportTransform {

void ExportContext::reset() {
	diagnostics.reset();
	string_resources.clear();
	obfuscated_string_literals.clear();
	next_string_resource_id = 1;
	obfuscation_random.randomize();
}

void ExportContext::set_options(const TransformOptions &p_options) {
	options = p_options;
}

const TransformOptions &ExportContext::get_options() const {
	return options;
}

uint32_t ExportContext::get_random_uint(uint32_t p_bounds) {
	return obfuscation_random.rand(p_bounds);
}

uint64_t ExportContext::create_string_resource(const String &p_value) {
	const uint64_t id = next_string_resource_id++;
	string_resources[id] = p_value;
	return id;
}

const HashMap<uint64_t, String> &ExportContext::get_string_resources() const {
	return string_resources;
}

String ExportContext::get_or_create_obfuscated_string_literal(Variant::Type p_type, const String &p_value) {
	const String key = String::num_int64(p_type) + ":" + p_value;
	if (const String *existing = obfuscated_string_literals.getptr(key)) {
		return *existing;
	}

	const String literal = WGodotGDScriptStringObfuscation::make_uncached_obfuscated_string_literal_source(*this, p_type, p_value);
	if (!literal.is_empty()) {
		obfuscated_string_literals[key] = literal;
	}
	return literal;
}

} // namespace WGodotGDScriptExportTransform
