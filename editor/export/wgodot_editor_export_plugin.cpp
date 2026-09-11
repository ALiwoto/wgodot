// wgodot-changes::file

#include "editor_export_plugin.h"

void EditorExportPlugin::set_export_error(Error p_error, const String &p_message) {
	export_error = p_error;
	get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, get_name(), p_message);
}

void EditorExportPlugin::_export_paths_ready(const HashSet<String> &p_paths) {
}

void EditorExportPlugin::_export_global_class_list(Array &r_global_class_list) {
}
