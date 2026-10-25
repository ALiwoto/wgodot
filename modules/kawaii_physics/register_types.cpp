// wgodot-changes::file
#include "register_types.h"

#include "kawaii_bone_simulator_3d.h"

#include "core/object/class_db.h"

void initialize_kawaii_physics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(KawaiiBoneCollision);
		GDREGISTER_CLASS(KawaiiBoneChain);
		GDREGISTER_CLASS(KawaiiBoneSimulationState);
		GDREGISTER_CLASS(KawaiiBoneSimulator3D);
	}
}

void uninitialize_kawaii_physics_module(ModuleInitializationLevel p_level) {
}
