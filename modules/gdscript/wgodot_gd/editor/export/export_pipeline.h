// wgodot-changes::file

#pragma once

#include "export_context.h"
#include "export_project.h"
#include "editor/export/editor_export_preset.h"

class EditorExportPlugin;

namespace WGodotGDScriptExportTransform {

class ExportPipeline {
	ExportProject original;
	ExportProject current;
	ExportContext artifacts;
	bool ready = false;

public:
	void prepare_export(EditorExportPlugin *p_plugin, const HashSet<String> &p_paths, const TransformOptions &p_options);
	Error complete_export(EditorExportPlugin *p_plugin, bool p_redact_diagnostics, const String &p_diagnostic_map_path);
	void export_file(EditorExportPlugin *p_plugin, const String &p_path, EditorExportPreset::ScriptExportMode p_script_mode);
	Error prepare(const HashSet<String> &p_paths, const TransformOptions &p_options, String &r_error);
	const ExportSource *get_source(const String &p_path) const { return ready ? current.get_source(p_path) : nullptr; }
	const ExportContext &get_artifacts() const { return artifacts; }
	void reset();
};

} // namespace WGodotGDScriptExportTransform
