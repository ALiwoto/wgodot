// wgodot-changes::file
/**************************************************************************/
/*  export_transform.h                                                    */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptExportTransform {

enum ObfuscationStrategy {
	OBFUSCATION_STRATEGY_SHORT,
	OBFUSCATION_STRATEGY_HASH,
	OBFUSCATION_STRATEGY_UNICODE,
};

struct TransformOptions {
	bool deconst_exports = true;
	bool obfuscate_local_variables = false;
	ObfuscationStrategy obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
};

TransformOptions setup_params();
String transform_source(const String &p_source, const String &p_path, bool *r_changed = nullptr);
String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed = nullptr);

} // namespace WGodotGDScriptExportTransform
