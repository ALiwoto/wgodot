// wgodot-changes::file

#include "export_pass_no_export.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptExportTransform {

Error NoExportPass::analyze(const ExportAnalysisInput &p_input, String &r_error) {
	excluded_ranges.clear();
	for (const String &path : p_input.project.get_script_paths()) {
		if (!p_input.project.is_exported(path)) {
			continue;
		}
		const String &source = p_input.project.get_source(path)->get_text();
		bool excluded = false;
		int line_number = 1;
		for (int start = 0; start < source.length();) {
			int end = start;
			while (end < source.length() && source[end] != '\n' && source[end] != '\r') {
				end++;
			}
			const String line = source.substr(start, end - start).strip_edges();
			const bool begin = line == "#wgodot::no_export::begin";
			const bool finish = line == "#wgodot::no_export::end";
			if (begin || finish || excluded) {
				SourceEdit range;
				range.start = start;
				range.end = end;
				if (start != end) {
					excluded_ranges[path].push_back(range);
				}
			}
			if (begin) {
				if (excluded) {
					WARN_PRINT(vformat("Nested #wgodot::no_export::begin in '%s' at line %d. Nested no_export blocks are not supported; ignoring nested begin marker.", path, line_number));
				}
				excluded = true;
			} else if (finish) {
				if (!excluded) {
					WARN_PRINT(vformat("Unmatched #wgodot::no_export::end in '%s' at line %d. Stripping the directive line anyway.", path, line_number));
				}
				excluded = false;
			}
			if (end < source.length() && source[end] == '\r') {
				end++;
			}
			if (end < source.length() && source[end] == '\n') {
				end++;
			}
			start = end;
			line_number++;
		}
		if (excluded) {
			WARN_PRINT(vformat("Unclosed #wgodot::no_export::begin in '%s'. Stripped until end of file.", path));
		}
	}
	return OK;
}

Error NoExportPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	r_output.edits = excluded_ranges;
	return OK;
}

} // namespace WGodotGDScriptExportTransform
