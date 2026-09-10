// wgodot-changes::file

#pragma once

#include "deadcode_injection.h"
#include "export_pass.h"

namespace WGodotGDScriptExportTransform {

class DeadCodePass : public ExportTransformationBase {
	HashMap<String, Vector<WGodotGDScriptDeadCodeInjection::Insertion>> insertions;

public:
	const char *get_name() const override { return "dead_code"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export") }; }
	bool is_enabled(const TransformOptions &p_options) const override;
	Error analyze(const ExportAnalysisInput &p_input, String &r_error) override;
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

} // namespace WGodotGDScriptExportTransform
