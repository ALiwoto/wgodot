// wgodot-changes::file
/**************************************************************************/
/*  export_transform.cpp                                                  */
/**************************************************************************/

#include "export_transform.h"

#include "deadcode_injection.h"
#include "export_context.h"
#include "export_no_export_blocks.h"
#include "export_timing.h"
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
	GLOBAL_DEF("wgodot/gdscript/disable_strict_type_checking_for_addons", true);
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
	GLOBAL_DEF("wgodot/export/timing_logs_enabled", false);
	GLOBAL_DEF("wgodot/export/timing_verbose_logs_enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/timing_slow_threshold_msec", PROPERTY_HINT_RANGE, "0,60000,1,or_greater"), 250);
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

void prescan_project_scripts(ExportContext *p_context, const HashSet<String> &p_paths) {
	if (p_context == nullptr) {
		return;
	}

	const uint64_t prescan_start_usec = export_timing_get_ticks_usec();
	ExportPrescanTimingStats timing_stats;
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

		const uint64_t script_start_usec = export_timing_get_ticks_usec();
		uint64_t phase_start_usec = export_timing_get_ticks_usec();
		const Vector<uint8_t> file = FileAccess::get_file_as_bytes(path);
		const uint64_t read_usec = export_timing_get_ticks_usec() - phase_start_usec;
		if (file.is_empty()) {
			continue;
		}

		timing_stats.script_count++;
		timing_stats.read_usec += read_usec;
		const String raw_source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
		phase_start_usec = export_timing_get_ticks_usec();
		const String export_source = strip_no_export_blocks(raw_source, path);
		const uint64_t strip_usec = export_timing_get_ticks_usec() - phase_start_usec;
		timing_stats.strip_usec += strip_usec;
		phase_start_usec = export_timing_get_ticks_usec();
		const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(export_source, path, options);
		const uint64_t deadcode_usec = export_timing_get_ticks_usec() - phase_start_usec;
		timing_stats.deadcode_usec += deadcode_usec;
		ScriptSource script;
		script.path = path;
		script.source = source;
		scripts.push_back(script);

		AnalyzedSource *analyzed_source = memnew(AnalyzedSource);
		String analysis_error;
		phase_start_usec = export_timing_get_ticks_usec();
		if (!analyzed_source->load(source, path, &analysis_error)) {
			const uint64_t analyze_usec = export_timing_get_ticks_usec() - phase_start_usec;
			timing_stats.analyze_usec += analyze_usec;
			export_timing_log_slow_phase(options, path, "prescan analyze failed", analyze_usec);
			WARN_PRINT("Failed to analyze wgodot-transformed GDScript during export prescan for '" + path + "'. Some export transforms may be incomplete for this script.\n" + analysis_error);
			reserve_script_global_class_name_from_source(p_context, source, path);
			memdelete(analyzed_source);
			continue;
		}
		const uint64_t analyze_usec = export_timing_get_ticks_usec() - phase_start_usec;
		timing_stats.analyze_usec += analyze_usec;
		scripts.write[scripts.size() - 1].analyzed_source = analyzed_source;

		if (options.obfuscate_names) {
			phase_start_usec = export_timing_get_ticks_usec();
			collect_global_class_rename_request(p_context, analyzed_source->parser->get_tree(), path, global_class_rename_requests);
			timing_stats.global_class_usec += export_timing_get_ticks_usec() - phase_start_usec;
		}
		if (options.obfuscate_builtin_names) {
			phase_start_usec = export_timing_get_ticks_usec();
			collect_builtin_class_aliases_from_node(p_context, analyzed_source->parser->get_tree());
			timing_stats.builtin_alias_usec += export_timing_get_ticks_usec() - phase_start_usec;
		}
		if (options.obfuscate_file_paths && has_obfuscate_path_annotation(analyzed_source->parser->get_tree())) {
			phase_start_usec = export_timing_get_ticks_usec();
			(void)p_context->get_or_create_script_path_rename(path);
			timing_stats.path_obfuscation_usec += export_timing_get_ticks_usec() - phase_start_usec;
		}

		const uint64_t script_usec = export_timing_get_ticks_usec() - script_start_usec;
		export_timing_log_slow_phase(options, path, "prescan script", script_usec);
	}

	if (options.obfuscate_names) {
		for (const GlobalClassRenameRequest &request : global_class_rename_requests) {
			(void)p_context->get_or_create_global_class_rename(request.name, request.path);
		}
	}

	if (options.obfuscate_names) {
		for (const ScriptSource &script : scripts) {
			if (script.analyzed_source == nullptr || script.analyzed_source->parser == nullptr) {
				continue;
			}
			p_context->reserve_script_member_names(script.analyzed_source->parser->get_tree());
		}
		for (const ScriptSource &script : scripts) {
			if (script.analyzed_source == nullptr || script.analyzed_source->parser == nullptr) {
				continue;
			}
			p_context->index_interface_methods(script.analyzed_source->parser->get_tree());
		}

		for (const ScriptSource &script : scripts) {
			if (script.analyzed_source == nullptr || script.analyzed_source->parser == nullptr) {
				continue;
			}

			export_timing_log_checkpoint(options, script.path, "index script begin");
			uint64_t phase_start_usec = export_timing_get_ticks_usec();
			p_context->index_script(script.analyzed_source->parser->get_tree(), script.path);
			const uint64_t index_script_usec = export_timing_get_ticks_usec() - phase_start_usec;
			timing_stats.index_script_usec += index_script_usec;
			if (export_timing_should_log_verbose(options)) {
				export_timing_log_slow_phase(options, script.path, "index script", index_script_usec, 1);
			}
		}
	}

	if (options.obfuscate_strings) {
		for (const ScriptSource &script : scripts) {
			if (script.analyzed_source == nullptr || script.analyzed_source->parser == nullptr) {
				continue;
			}
			export_timing_log_checkpoint(options, script.path, "string resource prescan begin");
			const uint64_t phase_start_usec = export_timing_get_ticks_usec();
			collect_string_obfuscation_resources_from_tree(script.source, script.path, script.analyzed_source->parser->get_tree(), p_context);
			const uint64_t string_resources_usec = export_timing_get_ticks_usec() - phase_start_usec;
			timing_stats.string_resources_usec += string_resources_usec;
			if (export_timing_should_log_verbose(options)) {
				export_timing_log_slow_phase(options, script.path, "string resource prescan", string_resources_usec, 1);
			}
		}
	}

	for (ScriptSource &script : scripts) {
		if (script.analyzed_source != nullptr) {
			memdelete(script.analyzed_source);
			script.analyzed_source = nullptr;
		}
	}

	timing_stats.total_usec = export_timing_get_ticks_usec() - prescan_start_usec;
	export_timing_log_prescan_summary(options, timing_stats);
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
	const uint64_t transform_start_usec = export_timing_get_ticks_usec();
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	bool no_export_changed = false;
	uint64_t phase_start_usec = export_timing_get_ticks_usec();
	const String export_source = strip_no_export_blocks(p_source, p_path, &no_export_changed);
	const uint64_t strip_usec = export_timing_get_ticks_usec() - phase_start_usec;

	if (!p_options.deconst_exports && !p_options.obfuscate_names && !p_options.obfuscate_builtin_names && !p_options.obfuscate_file_paths && !p_options.obfuscate_strings && !p_options.dead_code_injection_enabled && !p_options.strip_comments && !p_options.strip_empty_lines) {
		if (no_export_changed && r_changed != nullptr) {
			*r_changed = true;
		}
		return export_source;
	}

	bool dead_code_changed = false;
	phase_start_usec = export_timing_get_ticks_usec();
	const String source = WGodotGDScriptDeadCodeInjection::inject_in_class_dead_code(export_source, p_path, p_options, &dead_code_changed);
	const uint64_t deadcode_usec = export_timing_get_ticks_usec() - phase_start_usec;

	AnalyzedSource analyzed_source;
	String analysis_error;
	phase_start_usec = export_timing_get_ticks_usec();
	if (!analyzed_source.load(source, p_path, &analysis_error)) {
		const uint64_t analyze_usec = export_timing_get_ticks_usec() - phase_start_usec;
		export_timing_log_slow_phase(p_options, p_path, "transform analyze failed", analyze_usec);
		WARN_PRINT("Failed to analyze wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + analysis_error);
		return p_source;
	}
	const uint64_t analyze_usec = export_timing_get_ticks_usec() - phase_start_usec;

	ExportContext local_context;
	ExportContext *export_context = p_context;
	const bool using_local_context = export_context == nullptr && (p_options.obfuscate_names || p_options.obfuscate_builtin_names || p_options.obfuscate_file_paths || p_options.obfuscate_strings);
	if (using_local_context) {
		local_context.reset();
		local_context.set_options(p_options);
		export_context = &local_context;
	}
	if (using_local_context && p_options.obfuscate_builtin_names) {
		phase_start_usec = export_timing_get_ticks_usec();
		collect_builtin_class_aliases_from_node(export_context, analyzed_source.parser->get_tree());
		export_timing_log_slow_phase(p_options, p_path, "local builtin alias scan", export_timing_get_ticks_usec() - phase_start_usec);
	}

	const GDScriptParser::ClassNode *tree = analyzed_source.parser->get_tree();

	phase_start_usec = export_timing_get_ticks_usec();
	RewriteContext context;
	context.source = source;
	context.script_path = p_path;
	context.options = p_options;
	context.export_context = export_context;
	const bool timing_enabled = export_timing_should_log(p_options);
	context.timing_enabled = timing_enabled;
	context.obfuscation_random.randomize();
	if (context.export_context != nullptr) {
		context.export_context->reserve_script_global_class_name(tree);
		context.export_context->reserve_script_declaration_names_for_global_classes(tree);
		context.export_context->index_global_class_rename(tree, p_path);
		context.export_context->seed_reserved_obfuscated_names(context.reserved_obfuscated_names);
	}
	const uint64_t setup_usec = export_timing_get_ticks_usec() - phase_start_usec;
	phase_start_usec = export_timing_get_ticks_usec();
	ExportTransformTimingStats transform_timing;
	transform_timing.strip_usec = strip_usec;
	transform_timing.deadcode_usec = deadcode_usec;
	transform_timing.analyze_usec = analyze_usec;
	transform_timing.setup_usec = setup_usec;
	ExportReplacementTiming replacement_timing;
	if (timing_enabled) {
		reset_obfuscation_name_probe();
		set_obfuscation_name_probe_enabled(true);
	}
	collect_export_replacements(context, *analyzed_source.parser, timing_enabled ? &replacement_timing : nullptr);
	const uint64_t collect_replacements_usec = export_timing_get_ticks_usec() - phase_start_usec;
	if (timing_enabled) {
		set_obfuscation_name_probe_enabled(false);
	}
	const ObfuscationNameProbe name_probe = timing_enabled ? get_obfuscation_name_probe() : ObfuscationNameProbe();
	transform_timing.collect_replacements_usec = collect_replacements_usec;
	if (context.replacements.is_empty()) {
		if ((no_export_changed || dead_code_changed) && r_changed != nullptr) {
			*r_changed = true;
		}
		transform_timing.total_usec = export_timing_get_ticks_usec() - transform_start_usec;
		export_timing_log_transform_no_replacements(p_options, p_path, transform_timing, replacement_timing, context, name_probe);
		return source;
	}

	phase_start_usec = export_timing_get_ticks_usec();
	const String transformed = apply_replacements(context);
	const uint64_t apply_replacements_usec = export_timing_get_ticks_usec() - phase_start_usec;
	if (transformed == p_source) {
		return p_source;
	}

	String validation_error;
	phase_start_usec = export_timing_get_ticks_usec();
	if (!parse_only(transformed, p_path, &validation_error)) {
		const uint64_t validate_usec = export_timing_get_ticks_usec() - phase_start_usec;
		export_timing_log_slow_phase(p_options, p_path, "validate failed", validate_usec);
		WARN_PRINT("Failed to validate wgodot-transformed GDScript export for '" + p_path + "'. Exporting original script source.\n" + validation_error);
		return p_source;
	}
	const uint64_t validate_usec = export_timing_get_ticks_usec() - phase_start_usec;

	if (r_changed != nullptr) {
		*r_changed = true;
	}
	transform_timing.apply_replacements_usec = apply_replacements_usec;
	transform_timing.validate_usec = validate_usec;
	transform_timing.total_usec = export_timing_get_ticks_usec() - transform_start_usec;
	export_timing_log_transform_with_replacements(p_options, p_path, transform_timing, replacement_timing, context, name_probe, context.replacements.size());
	return transformed;
}

} // namespace WGodotGDScriptExportTransform
