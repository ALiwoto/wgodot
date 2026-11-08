// wgodot-changes::file
#pragma once

#include "core/os/mutex.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/curve.h"

class AudioStreamPlaybackGroup;

// Composable audio layers, sequences and choices. Null entries are silent choices.
class AudioStreamGroup : public AudioStream {
	GDCLASS(AudioStreamGroup, AudioStream);
	OBJ_SAVE_TYPE(AudioStream);
	friend class AudioStreamPlaybackGroup;

public:
	enum Mode { PARALLEL, SEQUENTIAL, RANDOM, SHUFFLE, SWITCH };

private:
	Mode mode = PARALLEL;
	TypedArray<AudioStream> streams;
	Vector<float> weights;
	Vector<float> volumes;
	Vector2 volume_range = Vector2(1, 1);
	Vector2 pitch_range = Vector2(1, 1);
	Vector2 delay_range;
	bool delay_uses_rate_scale = true;
	bool loop = false;
	StringName switch_parameter;
	int switch_offset = 0;
	int default_index = 0;
	StringName volume_parameter;
	Ref<Curve> volume_parameter_curve;
	bool volume_parameter_db = false;
	double volume_parameter_cutoff = -1.0;
	Vector<uint8_t> used;
	Mutex selection_mutex;

	int choose_index();
	float stream_volume(int p_index) const;
	double scaled_length(double p_rate) const;

protected:
	static void _bind_methods();

public:
	void set_mode(Mode p_value) { mode = p_value; }
	Mode get_mode() const { return mode; }
	void set_streams(const TypedArray<AudioStream> &p_value);
	TypedArray<AudioStream> get_streams() const { return streams; }
	void set_weights(const Vector<float> &p_value);
	Vector<float> get_weights() const { return weights; }
	void set_volumes(const Vector<float> &p_value) { volumes = p_value; }
	Vector<float> get_volumes() const { return volumes; }
	void set_volume_range(Vector2 p_value) { volume_range = p_value; }
	Vector2 get_volume_range() const { return volume_range; }
	void set_pitch_range(Vector2 p_value) { pitch_range = p_value; }
	Vector2 get_pitch_range() const { return pitch_range; }
	void set_delay_range(Vector2 p_value) { delay_range = p_value; }
	Vector2 get_delay_range() const { return delay_range; }
	void set_delay_uses_rate_scale(bool p_value) { delay_uses_rate_scale = p_value; }
	bool get_delay_uses_rate_scale() const { return delay_uses_rate_scale; }
	void set_loop(bool p_value) { loop = p_value; }
	bool get_loop() const { return loop; }
	void set_switch_parameter(const StringName &p_value) { switch_parameter = p_value; }
	StringName get_switch_parameter() const { return switch_parameter; }
	void set_switch_offset(int p_value) { switch_offset = p_value; }
	int get_switch_offset() const { return switch_offset; }
	void set_default_index(int p_value) { default_index = p_value; }
	int get_default_index() const { return default_index; }
	void set_volume_parameter(const StringName &p_value) { volume_parameter = p_value; }
	StringName get_volume_parameter() const { return volume_parameter; }
	void set_volume_parameter_curve(const Ref<Curve> &p_value) { volume_parameter_curve = p_value; }
	Ref<Curve> get_volume_parameter_curve() const { return volume_parameter_curve; }
	void set_volume_parameter_db(bool p_value) { volume_parameter_db = p_value; }
	bool get_volume_parameter_db() const { return volume_parameter_db; }
	void set_volume_parameter_cutoff(double p_value) { volume_parameter_cutoff = p_value; }
	double get_volume_parameter_cutoff() const { return volume_parameter_cutoff; }

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual double get_length() const override;
	virtual bool has_loop() const override;
	virtual bool is_meta_stream() const override { return true; }
	virtual void get_parameter_list(List<Parameter> *r_parameters) override;
};

VARIANT_ENUM_CAST(AudioStreamGroup::Mode);

class AudioStreamPlaybackGroup : public AudioStreamPlayback {
	GDCLASS(AudioStreamPlaybackGroup, AudioStreamPlayback);
	friend class AudioStreamGroup;
	Ref<AudioStreamGroup> group;
	Vector<Ref<AudioStreamPlayback>> children;
	HashMap<StringName, Variant> parameters;
	mutable Mutex parameter_mutex;
	bool active = false;
	bool children_started = false;
	int index = 0;
	int loops = 0;
	double delay = 0.0;
	double position = 0.0;
	double delay_duration = 0.0;
	bool initialized = false;
	bool seeking = false;
	int64_t cycle_frames = 0;
	float volume = 1.0;
	float pitch = 1.0;

	void start_child(int p_index);
	void start_children();
	void reset_for_seek();
	void finish_seek();
	void advance_to(double p_time);
	int mix_children(AudioFrame *p_buffer, float p_rate_scale, int p_frames);

protected:
	static void _bind_methods() {}

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override { return active; }
	virtual int get_loop_count() const override { return loops; }
	virtual double get_playback_position() const override { return position; }
	virtual void seek(double p_time) override;
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
	virtual void set_parameter(const StringName &p_name, const Variant &p_value) override;
	virtual Variant get_parameter(const StringName &p_name) const override;
	virtual void tag_used_streams() override;
};
