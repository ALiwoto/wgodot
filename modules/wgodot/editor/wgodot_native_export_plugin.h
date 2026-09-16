// wgodot-changes::file
#pragma once

#include "editor/export/editor_export_plugin.h"

class WGodotNativeExportPlugin : public EditorExportPlugin {
	GDCLASS(WGodotNativeExportPlugin, EditorExportPlugin);
	bool enabled = false;
	bool validated = false;
	Dictionary manifest;
	Dictionary autoloads;

protected:
	void _get_export_options(const Ref<EditorExportPlatform> &p_platform, List<EditorExportPlatform::ExportOption> *r_options) const override;
	void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	void _export_paths_ready(const HashSet<String> &p_paths) override;
	void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;
	void _export_global_class_list(Array &r_classes) override;
	void _export_project_settings(HashMap<String, Variant> &r_settings) override;
	void _export_cache_paths(HashSet<String> &r_paths) override;

public:
	String get_name() const override { return "0WGodotNative"; }
};
