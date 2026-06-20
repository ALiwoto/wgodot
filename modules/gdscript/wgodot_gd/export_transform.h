// wgodot-changes::file
/**************************************************************************/
/*  export_transform.h                                                    */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

class Array;

namespace WGodotGDScriptExportTransform {

class ExportContext;

enum ObfuscationStrategy {
	OBFUSCATION_STRATEGY_SHORT,
	OBFUSCATION_STRATEGY_HASH,
	OBFUSCATION_STRATEGY_UNICODE,
};

struct TransformOptions {
	bool deconst_exports = true;
	bool obfuscate_names = false;
	bool obfuscate_builtin_names = true;
	bool obfuscate_file_paths = false;
	bool obfuscate_strings = false;
	bool dead_code_injection_enabled = false;
	bool strip_comments = true;
	bool strip_empty_lines = true;
	bool binary_tokens_export = false;
	int min_in_class_dead_code_injection = 0;
	int max_in_class_dead_code_injection = 0;
	ObfuscationStrategy obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
	ObfuscationStrategy file_path_obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
};

void register_project_settings();
TransformOptions setup_params();
void prescan_project_scripts(ExportContext *p_context, const HashSet<String> &p_paths);
void transform_global_class_list(ExportContext *p_context, Array *r_global_class_list);
String transform_source(const String &p_source, const String &p_path, bool *r_changed = nullptr);
String transform_source(const String &p_source, const String &p_path, ExportContext *p_context, bool *r_changed = nullptr);
String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed = nullptr);

} // namespace WGodotGDScriptExportTransform
