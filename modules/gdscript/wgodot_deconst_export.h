// wgodot-changes::file
/**************************************************************************/
/*  wgodot_deconst_export.h                                               */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptDeconstExport {

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

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed = nullptr);
String sanitize_source(const String &p_source, const String &p_path, bool *r_changed = nullptr);

} // namespace WGodotGDScriptDeconstExport
