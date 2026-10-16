// wgodot-changes::file
#pragma once

#include "particle_instance_3d.h"
#include "scene/resources/packed_scene.h"

// Owns private particle hierarchies, prepared on a worker. Only the pool enters
// the SceneTree; particle visuals are published directly to the rendering server.
// Particle scenes contain transforms, geometry and lights, with no tree callbacks.
class ParticleInstancePool3D : public Node3D {
	GDCLASS(ParticleInstancePool3D, Node3D);

	Vector<ParticleInstance3D *> instances;
	Vector<int> available;
	Vector<uint8_t> acquired;
	HashMap<ParticleInstance3D *, int> indices;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	ParticleInstancePool3D();
	~ParticleInstancePool3D();
	Error prepare(const Ref<PackedScene> &p_scene, int p_capacity);
	ParticleInstance3D *acquire();
	void release(ParticleInstance3D *p_instance);
	TypedArray<ParticleInstance3D> get_instances() const;
	void set_instance_shader_parameter(const StringName &p_name, const Variant &p_value);
	int get_capacity() const { return instances.size(); }
	int get_available_count() const { return available.size(); }
};
