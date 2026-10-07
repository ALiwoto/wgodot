// wgodot-changes::file
#pragma once

#include "scene/3d/gpu_particles_3d.h"

// A manually evaluated emission interval, including particles surviving its end.
class CinematicParticles : public Node3D {
	GDCLASS(CinematicParticles, Node3D);

	TypedArray<GPUParticles3D> emitters;
	double start_time = 0.0;
	double end_time = 1.0;

protected:
	static void _bind_methods();

public:
	void set_emitters(const TypedArray<GPUParticles3D> &p_emitters) { emitters = p_emitters; }
	TypedArray<GPUParticles3D> get_emitters() const { return emitters; }
	void set_start_time(double p_time) { start_time = p_time; }
	double get_start_time() const { return start_time; }
	void set_end_time(double p_time) { end_time = p_time; }
	double get_end_time() const { return end_time; }

	void prepare();
	void seek(double p_seconds);
	void advance(double p_previous, double p_seconds);
};
