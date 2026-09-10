// wgodot-changes::file

#include "export_pass_dead_code.h"

namespace WGodotGDScriptExportTransform {

bool DeadCodePass::is_enabled(const TransformOptions &p_options) const {
	return p_options.dead_code_injection_enabled && p_options.max_dead_code_gaps_per_file > 0 && MAX(p_options.min_in_class_dead_code_injection, p_options.max_in_class_dead_code_injection) > 0;
}

Error DeadCodePass::analyze(const ExportAnalysisInput &p_input, String &r_error) {
	Error error = p_input.analysis.analyze_scripts(false, r_error);
	if (error != OK) {
		return error;
	}
	insertions.clear();
	for (const String &path : p_input.project.get_script_paths()) {
		if (!p_input.project.is_exported(path)) {
			continue;
		}
		Ref<GDScriptParserRef> parser = p_input.analysis.get_parser(path, GDScriptParserRef::PARSED, error);
		WGodotGDScriptDeadCodeInjection::analyze_in_class_dead_code(p_input.project.get_source(path)->get_text(), parser->get_parser()->get_tree(), insertions[path]);
	}
	return OK;
}

Error DeadCodePass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *script_insertions = insertions.getptr(path);
		if (script_insertions != nullptr) {
			WGodotGDScriptDeadCodeInjection::make_in_class_dead_code_edits(p_input.project.get_source(path)->get_text(), path, p_input.get_options(), *script_insertions, r_output.edits[path]);
		}
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
