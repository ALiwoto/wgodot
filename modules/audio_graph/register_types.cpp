// wgodot-changes::file
#include "register_types.h"
#include "audio_stream_group.h"
#include "core/object/class_db.h"

void initialize_audio_graph_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(AudioStreamGroup);
		GDREGISTER_ABSTRACT_CLASS(AudioStreamPlaybackGroup);
	}
}

void uninitialize_audio_graph_module(ModuleInitializationLevel p_level) {
}
