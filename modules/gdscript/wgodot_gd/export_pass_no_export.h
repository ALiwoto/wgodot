// wgodot-changes::file

#pragma once

#include "export_pass.h"

namespace WGodotGDScriptExportTransform {

class NoExportPass : public ExportTransformationBase {
	HashMap<String, Vector<SourceEdit>> excluded_ranges;

public:
	const char *get_name() const override { return "no_export"; }
	bool is_enabled(const TransformOptions &p_options) const override { return true; }
	Error analyze(const ExportAnalysisInput &p_input, String &r_error) override;
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

} // namespace WGodotGDScriptExportTransform
