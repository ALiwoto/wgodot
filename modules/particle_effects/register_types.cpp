// wgodot-changes::file
#include "core/object/class_db.h"
#include "register_types.h"

#include "particle_curves.h"
#include "particle_instance_3d.h"
#include "random_stream_32.h"
#include "particle_instance_pool_3d.h"
#include "particle_simulation_3d.h"
#include "scene_preparation.h"
#include "scene_work_queue.h"
#include "visual_layer_scope_3d.h"

void initialize_particle_effects_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(RandomStream32);
	GDREGISTER_CLASS(ParticleFloatCurve);
	GDREGISTER_CLASS(ParticleVectorCurve3D);
	GDREGISTER_CLASS(ParticleSeed);
	GDREGISTER_CLASS(ParticleVelocityRule3D);
	GDREGISTER_CLASS(ParticleSpawnShape3D);
	GDREGISTER_CLASS(ParticleBirthRule3D);
	GDREGISTER_CLASS(ParticleColorCurve);
	GDREGISTER_CLASS(ParticleOrientation3D);
	GDREGISTER_CLASS(ParticleMaterialChannels);
	GDREGISTER_CLASS(ParticleMotionProfile3D);
	GDREGISTER_CLASS(ParticleSpriteProfile3D);
	GDREGISTER_CLASS(ParticleMeshProfile3D);
	GDREGISTER_CLASS(ParticleLightProfile3D);
	GDREGISTER_CLASS(ParticleGroundProjector3D);
	GDREGISTER_CLASS(ParticleShaderBindings);
	GDREGISTER_CLASS(ParticleSpawnContext3D);
	GDREGISTER_CLASS(ParticleInstance3D);
	ParticleInstance3D::initialize_defaults();
	GDREGISTER_CLASS(ParticleInstancePool3D);
	GDREGISTER_CLASS(ParticleSimulation3D);
	GDREGISTER_CLASS(ScenePreparation);
	GDREGISTER_CLASS(SceneWorkItem);
	GDREGISTER_CLASS(SceneWorkQueue);
	GDREGISTER_CLASS(VisualLayerScope3D);
}

void uninitialize_particle_effects_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		ParticleInstance3D::clear_defaults();
	}
}
