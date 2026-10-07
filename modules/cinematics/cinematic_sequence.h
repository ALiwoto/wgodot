// wgodot-changes::file
#pragma once

#include "cinematic_shot.h"

class CinematicSequence : public Node3D {
	GDCLASS(CinematicSequence, Node3D);

	TypedArray<CinematicShot> shots;
	ObjectID presentation_id;
	StringName animation = "opening";
	bool autoplay = true;
	bool prepared = false;
	bool playing = false;
	double length = 0.0;
	double time_seconds = 0.0;
	int shot_index = -1;
	double shot_start = 0.0;

	void _select_shot();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_shots(const TypedArray<CinematicShot> &p_shots);
	TypedArray<CinematicShot> get_shots() const { return shots; }
	void set_presentation(AnimationPlayer *p_player);
	AnimationPlayer *get_presentation() const;
	void set_animation(const StringName &p_animation) { animation = p_animation; }
	StringName get_animation() const { return animation; }
	void set_autoplay(bool p_autoplay) { autoplay = p_autoplay; }
	bool is_autoplay() const { return autoplay; }

	double get_length() const { return length; }
	double get_time_seconds() const { return time_seconds; }
	int get_shot_index() const { return shot_index; }
	int get_shot_count() const { return shots.size(); }
	CinematicShot *get_shot(int p_index) const;
	double get_shot_start(int p_index) const;
	bool is_playing() const { return playing; }
	bool prepare();
	void advance_playback(double p_delta);
	void show_frame(double p_seconds);
	void play();
	void pause();
};
