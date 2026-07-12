// wgodot-changes::file
/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#ifdef TOOLS_ENABLED
#include "editor/wgodot_cli_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_wgodot_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<WGodotCLIEditorPlugin>();
	}
#endif
}

void uninitialize_wgodot_module(ModuleInitializationLevel p_level) {
}
