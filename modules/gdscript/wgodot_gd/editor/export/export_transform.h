// wgodot-changes::file
/**************************************************************************/
/*  export_transform.h                                                    */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptExportTransform {

struct TransformOptions {
	bool deconst_exports = true;
	bool obfuscate_strings = false;
	bool redact_diagnostics = false;
	bool strip_comments = true;
	bool strip_empty_lines = true;
	bool timing_logs_enabled = false;
	bool timing_verbose_logs_enabled = false;
	int timing_slow_threshold_msec = 250;
};

TransformOptions setup_params();

} // namespace WGodotGDScriptExportTransform
