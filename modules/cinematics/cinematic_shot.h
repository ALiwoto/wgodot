// wgodot-changes::file
#pragma once

#include "cinematic_particles.h"

#include "scene/3d/camera_3d.h"
#include "scene/animation/animation_player.h"

class CinematicShot : public Node3D {
	GDCLASS(CinematicShot, Node3D);

	double length = 1.0;
	double time_seconds = 0.0;
	StringName animation = "opening";
	TypedArray<AnimationPlayer> players;
	TypedArray<CinematicParticles> effects;
	ObjectID camera_id;

protected:
	static void _bind_methods();

public:
	void set_length(double p_length);
	double get_length() const { return length; }
	void set_animation(const StringName &p_animation) { animation = p_animation; }
	StringName get_animation() const { return animation; }
	void set_players(const TypedArray<AnimationPlayer> &p_players) { players = p_players; }
	TypedArray<AnimationPlayer> get_players() const { return players; }
	void set_effects(const TypedArray<CinematicParticles> &p_effects) { effects = p_effects; }
	TypedArray<CinematicParticles> get_effects() const { return effects; }
	void set_camera(Camera3D *p_camera);
	Camera3D *get_camera() const;

	bool prepare();
	void activate(double p_seconds);
	void advance(double p_delta);
	void deactivate();
};
