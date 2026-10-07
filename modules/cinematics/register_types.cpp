// wgodot-changes::file
#include "register_types.h"

#include "cinematic_particles.h"
#include "cinematic_sequence.h"
#include "cinematic_shot.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/cinematics_editor_plugin.h"
#endif

void initialize_cinematics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(CinematicParticles);
		GDREGISTER_CLASS(CinematicShot);
		GDREGISTER_CLASS(CinematicSequence);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(CinematicsEditorPanel);
		GDREGISTER_ABSTRACT_CLASS(CinematicsEditorPlugin);
		EditorPlugins::add_by_type<CinematicsEditorPlugin>();
	}
#endif
}

void uninitialize_cinematics_module(ModuleInitializationLevel p_level) {
}
