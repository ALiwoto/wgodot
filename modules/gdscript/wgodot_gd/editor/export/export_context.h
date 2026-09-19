// wgodot-changes::file
/**************************************************************************/
/*  export_context.h                                                      */
/**************************************************************************/

#pragma once

#include "export_diagnostics.h"
#include "export_transform.h"

#include "core/math/random_pcg.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/typedefs.h"
#include "core/variant/variant.h"

namespace WGodotGDScriptExportTransform {

class ExportContext {
	TransformOptions options;
	DiagnosticExport diagnostics;
	HashMap<uint64_t, String> string_resources;
	HashMap<String, String> obfuscated_string_literals;
	uint64_t next_string_resource_id = 1;
	RandomPCG obfuscation_random;

public:
	void reset();
	void set_options(const TransformOptions &p_options);
	const TransformOptions &get_options() const;
	DiagnosticExport &get_diagnostics() { return diagnostics; }
	const DiagnosticExport &get_diagnostics() const { return diagnostics; }
	uint32_t get_random_uint(uint32_t p_bounds);
	uint64_t create_string_resource(const String &p_value);
	const HashMap<uint64_t, String> &get_string_resources() const;
	String get_or_create_obfuscated_string_literal(Variant::Type p_type, const String &p_value);
};

} // namespace WGodotGDScriptExportTransform
