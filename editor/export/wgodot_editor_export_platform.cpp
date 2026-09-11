// wgodot-changes::file

#include "editor_export_platform.h"
#include "editor_export.h"
#include "editor_export_plugin.h"

Error EditorExportPlatform::ExportNotifier::finish(Error p_result) {
	if (!enabled || p_result != OK) {
		return p_result;
	}
	const Vector<Ref<EditorExportPlugin>> plugins = EditorExport::get_singleton()->get_export_plugins();
	for (const Ref<EditorExportPlugin> &plugin : plugins) {
		if (plugin->export_error != OK) {
			return plugin->export_error;
		}
	}
	for (const Ref<EditorExportPlugin> &plugin : plugins) {
		const Error error = plugin->_export_completed();
		if (error != OK) {
			return error;
		}
	}
	return OK;
}
