// wgodot-changes::file

#pragma once

#include "export_context.h"
#include "export_project.h"

namespace WGodotGDScriptExportTransform {

class ExportPipeline {
	ExportProject original;
	ExportProject current;
	ExportContext artifacts;
	bool ready = false;

public:
	Error prepare(const HashSet<String> &p_paths, const TransformOptions &p_options, String &r_error);
	const ExportSource *get_source(const String &p_path) const { return ready ? current.get_source(p_path) : nullptr; }
	const ExportContext &get_artifacts() const { return artifacts; }
	void reset();
};

} // namespace WGodotGDScriptExportTransform
