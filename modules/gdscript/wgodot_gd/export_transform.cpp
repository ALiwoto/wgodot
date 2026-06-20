// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "deadcode_injection.h"
#include "export_context.h"
#include "export_transform_internal.h"
#include "obfuscation_names.h"
#include "source_rewrite.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace WGodotGDScriptExportTransform {

static String transform_source_with_options(const String &p_source, const String &p_path, const TransformOptions &p_options, ExportContext *p_context, bool *r_changed);

void register_project_settings() {
	GLOBAL_DEF("wgodot/gdscript/disable_embedded_gdscript", true);
	GLOBAL_DEF("wgodot/gdscript/strict_override_checking", true);
	GLOBAL_DEF("wgodot/gdscript/strict_type_checking", true);
	GLOBAL_DEF("wgodot/gdscript/strict_signal_callable_checking", true);
	GLOBAL_DEF("wgodot/export/deconst_exports", true);
	GLOBAL_DEF("wgodot/export/obfuscate_names", true);
	GLOBAL_DEF("wgodot/export/obfuscate_builtin_names", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/obfuscation_strategy", PROPERTY_HINT_ENUM, "Short,Hash,Unicode"), OBFUSCATION_STRATEGY_SHORT);
	GLOBAL_DEF("wgodot/export/obfuscate_file_paths", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/obfuscate_file_paths_strategy", PROPERTY_HINT_ENUM, "Short,Hash,Unicode"), OBFUSCATION_STRATEGY_SHORT);
	GLOBAL_DEF("wgodot/export/obfuscate_strings", true);
	GLOBAL_DEF("wgodot/export/dead_code_injection_enabled", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/min_in_class_dead_code_injection", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), 10);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/max_in_class_dead_code_injection", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), 20);
}

TransformOptions setup_params() {
	TransformOptions options;
	options.deconst_exports = GLOBAL_GET_CACHED(bool, "wgodot/export/deconst_exports");
	options.obfuscate_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_names");
	options.obfuscate_builtin_names = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_builtin_names");
	options.obfuscate_file_paths = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_file_paths");
	options.obfuscate_strings = GLOBAL_GET_CACHED(bool, "wgodot/export/obfuscate_strings");
	options.dead_code_injection_enabled = GLOBAL_GET_CACHED(bool, "wgodot/export/dead_code_injection_enabled");
	options.min_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/min_in_class_dead_code_injection");
	options.max_in_class_dead_code_injection = GLOBAL_GET_CACHED(int, "wgodot/export/max_in_class_dead_code_injection");

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

void prescan_project_scripts(ExportContext *p_context, const HashSet<String> &p_paths) {
	if (p_context == nullptr) {
		return;
	}

	const TransformOptions &options = p_context->get_options();
	if (!options.obfuscate_names && !options.obfuscate_builtin_names && !options.obfuscate_file_paths && !options.obfuscate_strings && !options.dead_code_injection_enabled) {
		return;
	}

	Vector<ScriptSource> scripts;
	Vector<GlobalClassRenameRequest> global_class_rename_requests;
	for (const String &path : p_paths) {
		p_context->reserve_script_path(path);

		if (path.get_extension() != "gd") {
			continue;
		}

		const Vector<uint8_t> file = FileAccess::get_file_as_bytes(path);
		if (file.is_empty()) {
			continue;
		}

		const String raw_source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
		const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(raw_source, path, options);
		ScriptSource script;
		script.path = path;
		script.source = source;
		scripts.push_back(script);

		AnalyzedSource analyzed_source;
		String analysis_error;
		if (!analyzed_source.load(source, path, &analysis_error)) {
			WARN_PRINT("Failed to analyze wgodot-transformed GDScript during export prescan for '" + path + "'. Some export transforms may be incomplete for this script.\n" + analysis_error);
			reserve_script_global_class_name_from_source(p_context, source, path);
			continue;
		}

		if (options.obfuscate_names) {
			collect_global_class_rename_request(p_context, analyzed_source.parser->get_tree(), path, global_class_rename_requests);
		}
		if (options.obfuscate_builtin_names) {
			collect_builtin_class_aliases_from_node(p_context, analyzed_source.parser->get_tree());
		}
		if (options.obfuscate_file_paths && has_obfuscate_path_annotation(analyzed_source.parser->get_tree())) {
			(void)p_context->get_or_create_script_path_rename(path);
		}
	}

	if (options.obfuscate_names) {
		for (const GlobalClassRenameRequest &request : global_class_rename_requests) {
			(void)p_context->get_or_create_global_class_rename(request.name, request.path);
		}
	}

	if (options.obfuscate_names) {
		for (const ScriptSource &script : scripts) {
			AnalyzedSource analyzed_source;
			String analysis_error;
			if (!analyzed_source.load(script.source, script.path, &analysis_error)) {
				WARN_PRINT("Failed to analyze wgodot-transformed GDScript while indexing export names for '" + script.path + "'. Some name obfuscation may be incomplete for this script.\n" + analysis_error);
				continue;
			}

			p_context->index_script(analyzed_source.parser->get_tree(), script.path);
		}
	}

	if (options.obfuscate_strings) {
		for (const ScriptSource &script : scripts) {
			collect_string_obfuscation_resources(script.source, script.path, p_context);
		}
	}
}

void transform_global_class_list(ExportContext *p_context, Array *r_global_class_list) {
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

String transform_source(const String &p_source, const String &p_path, bool *r_changed) {
	return transform_source_with_options(p_source, p_path, setup_params(), nullptr, r_changed);
}

String transform_source(const String &p_source, const String &p_path, ExportContext *p_context, bool *r_changed) {
	const TransformOptions options = p_context != nullptr ? p_context->get_options() : setup_params();
	return transform_source_with_options(p_source, p_path, options, p_context, r_changed);
}

String transform_source(const String &p_source, const String &p_path, const TransformOptions &p_options, bool *r_changed) {
	return transform_source_with_options(p_source, p_path, p_options, nullptr, r_changed);
}

static String transform_source_with_options(const String &p_source, const String &p_path, const TransformOptions &p_options, ExportContext *p_context, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.deconst_exports && !p_options.obfuscate_names && !p_options.obfuscate_builtin_names && !p_options.obfuscate_file_paths && !p_options.obfuscate_strings && !p_options.dead_code_injection_enabled && !p_options.strip_comments && !p_options.strip_empty_lines) {
		return p_source;
	}

	bool dead_code_changed = false;
	const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(p_source, p_path, p_options, &dead_code_changed);

	AnalyzedSource analyzed_source;
	String analysis_error;
	if (!analyzed_source.load(source, p_path, &analysis_error)) {
		WARN_PRINT("Failed to analyze wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + analysis_error);
		return p_source;
	}

	ExportContext local_context;
	ExportContext *export_context = p_context;
	const bool using_local_context = export_context == nullptr && (p_options.obfuscate_names || p_options.obfuscate_builtin_names || p_options.obfuscate_file_paths || p_options.obfuscate_strings);
	if (using_local_context) {
		local_context.reset();
		local_context.set_options(p_options);
		export_context = &local_context;
	}
	if (using_local_context && p_options.obfuscate_builtin_names) {
		collect_builtin_class_aliases_from_node(export_context, analyzed_source.parser->get_tree());
	}

	const GDScriptParser::ClassNode *tree = analyzed_source.parser->get_tree();

	RewriteContext context;
	context.source = source;
	context.script_path = p_path;
	context.options = p_options;
	context.export_context = export_context;
	context.obfuscation_random.randomize();
	if (context.export_context != nullptr) {
		context.export_context->reserve_script_global_class_name(tree);
		context.export_context->reserve_script_declaration_names_for_global_classes(tree);
		context.export_context->index_global_class_rename(tree, p_path);
		context.export_context->seed_reserved_obfuscated_names(context.reserved_obfuscated_names);
	}
	collect_export_replacements(context, *analyzed_source.parser);
	if (context.replacements.is_empty()) {
		if (dead_code_changed && r_changed != nullptr) {
			*r_changed = true;
		}
		return source;
	}

	const String transformed = apply_replacements(context);
	if (transformed == p_source) {
		return p_source;
	}

	String validation_error;
	if (!parse_only(transformed, p_path, &validation_error)) {
		WARN_PRINT("Failed to validate wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + validation_error);
		return p_source;
	}

	if (r_changed != nullptr) {
		*r_changed = true;
	}
	return transformed;
}

} // namespace WGodotGDScriptExportTransform
