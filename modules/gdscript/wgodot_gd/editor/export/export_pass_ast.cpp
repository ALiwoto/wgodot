// wgodot-changes::file

#include "export_pass_ast.h"

namespace WGodotGDScriptExportTransform {

Error AnalyzedExportPass::analyze(const ExportAnalysisInput &p_input, String &r_error) {
	Error error = p_input.analysis.analyze_scripts(true, r_error);
	if (error != OK) {
		return error;
	}
	analyzed_scripts.clear();
	for (const String &path : p_input.project.get_script_paths()) {
		if (p_input.project.is_exported(path)) {
			analyzed_scripts[path] = p_input.analysis.get_parser(path, GDScriptParserRef::FULLY_SOLVED, error);
		}
	}
	return OK;
}

void AnalyzedExportPass::setup_rewrite(const ExportPassInput &p_input, ExportPassOutput &r_output, const String &p_path, RewriteContext &r_rewrite) const {
	r_rewrite.source = p_input.project.get_source(p_path)->get_text();
	r_rewrite.script_path = p_path;
	r_rewrite.export_context = &r_output.artifacts;
	r_rewrite.options = p_input.get_options();
	r_rewrite.options.deconst_exports = false;
	r_rewrite.options.obfuscate_names = false;
	r_rewrite.options.obfuscate_builtin_names = false;
	r_rewrite.options.obfuscate_file_paths = false;
	r_rewrite.options.obfuscate_strings = false;
	r_rewrite.options.strip_comments = false;
	r_rewrite.options.strip_empty_lines = false;
	build_line_offsets(r_rewrite);
}

void AnalyzedExportPass::finish_rewrite(const String &p_path, const RewriteContext &p_rewrite, ExportPassOutput &r_output) const {
	r_output.edits[p_path] = p_rewrite.replacements;
}

} // namespace WGodotGDScriptExportTransform
