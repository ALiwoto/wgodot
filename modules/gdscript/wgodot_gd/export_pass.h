// wgodot-changes::file

#pragma once

#include "export_analysis.h"
#include "export_context.h"
#include "export_project.h"

namespace WGodotGDScriptExportTransform {

struct ExportPassInput {
	const ExportProject &project;
	const ExportContext &artifacts;

	const TransformOptions &get_options() const { return artifacts.get_options(); }
};

struct ExportAnalysisInput : ExportPassInput {
	// Prescan retains copied facts; its frontend state ends with the original
	// phase. Each pass's current frontend state lives through its transform.
	ExportAnalysis &analysis;
};

struct ExportPassOutput {
	HashMap<String, Vector<SourceEdit>> edits;
	ExportContext artifacts;

	explicit ExportPassOutput(const ExportContext &p_artifacts) : artifacts(p_artifacts) {}
};

class ExportTransformationBase {
public:
	virtual const char *get_name() const = 0;
	virtual bool is_enabled(const TransformOptions &p_options) const = 0;
	virtual Vector<StringName> get_predecessors() const { return {}; }
	virtual Error prescan(const ExportAnalysisInput &p_original, String &r_error) { return OK; }
	virtual Error analyze(const ExportAnalysisInput &p_input, String &r_error) { return OK; }
	virtual Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) = 0;
	virtual ~ExportTransformationBase() = default;
};

} // namespace WGodotGDScriptExportTransform
