// wgodot-changes::file
/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"
#include "animation/affine_skeleton_2d.h"
#include "animation/skinned_mesh_2d.h"
#include "codecs/animation_codec.h"
#include "codecs/curve_codec.h"
#include "codecs/shader_material_codec.h"
#include "codecs/resource_load_measurement.h"
#include "object_metadata.h"
#include "core/object/class_db.h"

#include "wgodot_game_bridge.h"
#include "wgodot_pause_controller.h"
#include "wgodot_preloads.h"
#include "wgodot_wait_controller.h"


#include "modules/modules_enabled.gen.h"
#ifdef MODULE_GDSCRIPT_ENABLED
#include "native/binary_serializable.h"

#include "core/object/wgodot_interface_registry.h"
#endif

#ifdef TOOLS_ENABLED
#include "core/object/class_db.h"
#include "wgodot_particle_snapshot.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/wgodot_cli_editor_plugin.h"
#include "editor/wgodot_native_export_plugin.h"

static void _native_export_editor_init() {
	Ref<WGodotNativeExportPlugin> plugin;
	plugin.instantiate();
	EditorExport::get_singleton()->add_export_plugin(plugin);
}
#endif

void initialize_wgodot_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(AffineBone2D);
		GDREGISTER_CLASS(AffineSkeleton2D);
		GDREGISTER_CLASS(Skin2D);
		GDREGISTER_CLASS(SkinnedMesh2D);
		GDREGISTER_CLASS(WAnimation);
		register_animation_codec();
		register_curve_codec();
		register_shader_material_codec();
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
		GDREGISTER_CLASS(WResourceLoadMeasurement);
#endif
		GDREGISTER_ABSTRACT_CLASS(ObjectMetadata);
#ifdef TOOLS_ENABLED
		GDREGISTER_CLASS(WGPUParticleSnapshot);
#endif
		WGodotPreloads::initialize();
#ifdef MODULE_GDSCRIPT_ENABLED
		WGDREGISTER_INTERFACE(BinarySerializable, "Object", "modules/wgodot/native/binary_serializable.h");
#endif
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
		unregister_shader_material_codec();
		unregister_curve_codec();
		unregister_animation_codec();
		WGodotPreloads::deinitialize();
		WGodotGameBridge::deinitialize();
		WGodotWaitController::deinitialize();
		WGodotPauseController::deinitialize();
	}
}
