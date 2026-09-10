// wgodot-changes::file

#pragma once

#include "export_pass.h"

namespace WGodotGDScriptExportTransform {

class DiagnosticPass : public ExportTransformationBase {
	struct Call {
		int start = 0;
		int end = 0;
		int original_line = 0;
		String function;
		String message;
	};
	HashMap<String, HashSet<int>> original_calls;
	HashMap<String, Vector<Call>> current_calls;

public:
	const char *get_name() const override { return "diagnostics"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.redact_diagnostics; }
	Error prescan(const ExportAnalysisInput &p_original, String &r_error) override;
	Error analyze(const ExportAnalysisInput &p_input, String &r_error) override;
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

} // namespace WGodotGDScriptExportTransform
