// wgodot-changes::file
/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#include "wgodot_game_bridge.h"
#include "wgodot_pause_controller.h"
#include "wgodot_wait_controller.h"

#ifdef TOOLS_ENABLED
#include "editor/wgodot_cli_editor_plugin.h"
#include "editor/wgodot_native_export_plugin.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/plugins/editor_plugin.h"

static void _native_export_editor_init() {
	Ref<WGodotNativeExportPlugin> plugin;
	plugin.instantiate();
	EditorExport::get_singleton()->add_export_plugin(plugin);
}
#endif

void initialize_wgodot_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		WGodotPauseController::initialize();
		WGodotWaitController::initialize();
		WGodotGameBridge::initialize();
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<WGodotCLIEditorPlugin>();
		EditorNode::add_init_callback(_native_export_editor_init);
	}
#endif
}

void uninitialize_wgodot_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		WGodotGameBridge::deinitialize();
		WGodotWaitController::deinitialize();
		WGodotPauseController::deinitialize();
	}
}
