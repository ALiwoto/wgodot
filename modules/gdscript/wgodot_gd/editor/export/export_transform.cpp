// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "export_context.h"
#include "obfuscation_names.h"

#include "core/config/project_settings.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace WGodotGDScriptExportTransform {

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "wgodot/export/deconst_exports");
	options.obfuscate_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_names");
	options.obfuscate_builtin_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_builtin_names");
	options.obfuscate_file_paths = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_file_paths");
	options.obfuscate_strings = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_strings");
	options.redact_diagnostics = GLOBAL_GET_CACHED(bool, "wgodot/export/redact_diagnostics");
	options.dead_code_injection_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/dead_code_injection_enabled");
	options.min_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/min_in_class_dead_code_injection");
	options.max_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/max_in_class_dead_code_injection");
	options.max_dead_code_gaps_per_file = GLOBAL_GET_CACHED(int, "wgodot/export/max_dead_code_gaps_per_file");
	options.timing_logs_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/timing_logs_enabled");
	options.timing_verbose_logs_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/timing_verbose_logs_enabled");
	options.timing_slow_threshold_msec = GLOBAL_GET_CACHED(int, "wgodot/export/timing_slow_threshold_msec");

	const int obfuscation_strategy = GLOBAL_GET_CACHED(int, "wgodot/export/obfuscation_strategy");
	if (obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.obfuscation_strategy = static_cast<ObfuscationStrategy>(obfuscation_strategy);
	}

	const int file_path_obfuscation_strategy = GLOBAL_GET_CACHED(int, "wgodot/export/obfuscate_file_paths_strategy");
	if (file_path_obfuscation_strategy >= OBFUSCATION_STRATEGY_SHORT && file_path_obfuscation_strategy <= OBFUSCATION_STRATEGY_UNICODE) {
		options.file_path_obfuscation_strategy = static_cast<ObfuscationStrategy>(file_path_obfuscation_strategy);
	}

	return options;
}

void transform_global_class_list(const ExportContext *p_context, Array *r_global_class_list) {
	if (p_context == nullptr || r_global_class_list == nullptr) {
		return;
	}

	for (int i = 0; i < r_global_class_list->size(); i++) {
		Dictionary class_dict = (*r_global_class_list)[i];

		if (class_dict.has("class")) {
			const StringName class_name = class_dict["class"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename(class_name)) {
				class_dict["class"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
		}

		if (class_dict.has("base")) {
			const StringName base_name = class_dict["base"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename(base_name)) {
				class_dict["base"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
		}

		if (class_dict.has("path")) {
			const String path = class_dict["path"];
			if (const StringName *obfuscated_name = p_context->get_global_class_rename_by_path(path)) {
				class_dict["class"] = StringName(unwrap_binary_identifier_escape(String(*obfuscated_name)));
			}
			const String obfuscated_path = p_context->get_exported_script_path(path);
			if (!obfuscated_path.is_empty()) {
				class_dict["path"] = obfuscated_path;
			}
		}

		(*r_global_class_list)[i] = class_dict;
	}
}

} // namespace WGodotGDScriptExportTransform
