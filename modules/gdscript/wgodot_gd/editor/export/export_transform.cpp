// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "core/config/project_settings.h"

namespace WGodotGDScriptExportTransform {

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "wgodot/export/deconst_exports");
	options.obfuscate_strings = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_strings");
	options.redact_diagnostics = GLOBAL_GET_CACHED(bool, "wgodot/export/redact_diagnostics");
	options.timing_logs_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/timing_logs_enabled");
	options.timing_verbose_logs_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/timing_verbose_logs_enabled");
	options.timing_slow_threshold_msec = GLOBAL_GET_CACHED(int, "wgodot/export/timing_slow_threshold_msec");

	return options;
}

} // namespace WGodotGDScriptExportTransform
