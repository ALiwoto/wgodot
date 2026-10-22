// wgodot-changes::file
#pragma once

#include "core/io/resource.h"

// Periodic one-dimensional gradient noise with authored lattice gradients.
// Keeping the lattice in a resource makes imported procedural motion
// reproducible without depending on another noise generator's seed/hash.
class GradientNoise1D : public Resource {
	GDCLASS(GradientNoise1D, Resource);

	Vector<float> gradients;
	float amplitude = 1.0f;

protected:
	static void _bind_methods();

public:
	void set_gradients(const Vector<float> &p_gradients);
	Vector<float> get_gradients() const;
	void set_amplitude(float p_amplitude);
	float get_amplitude() const;
	float sample(float p_position) const;
};
