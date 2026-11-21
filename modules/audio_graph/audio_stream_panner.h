// wgodot-changes::file
#pragma once

#include "core/math/transform_3d.h"
#include "core/math/vector4.h"
#include "core/os/mutex.h"
#include "scene/resources/audio/audio_stream.h"

class AudioStreamPlaybackPanner;

// Per-stream routing preserves mono/stereo identity before a cue mixes its
// branches. The ordinary 3D player provides listener-relative coordinates.
class AudioStreamPanner : public AudioStream {
	GDCLASS(AudioStreamPanner, AudioStream);
	OBJ_SAVE_TYPE(AudioStream);
	friend class AudioStreamPlaybackPanner;
	friend class WAudioPlaybackProbe;

public:
	enum Mode { DISABLED, LINEAR, EQUAL_POWER };
	struct Settings {
		int input_channels = 1;
		Mode mode = DISABLED;
		float mono_gain = 1.0f;
		float omnidirectional_radius = 0.0f;
		float stereo_spread = 0.0f;
		StringName stop_on_muted_bus;
	};

private:
	Ref<AudioStream> stream;
	Settings settings;

protected:
	static void _bind_methods();

public:
	void set_stream(const Ref<AudioStream> &p_stream);
	Ref<AudioStream> get_stream() const { return stream; }
	void set_input_channels(int p_channels);
	int get_input_channels() const { return settings.input_channels; }
	void set_mode(Mode p_mode);
	Mode get_mode() const { return settings.mode; }
	void set_mono_gain(float p_gain);
	float get_mono_gain() const { return settings.mono_gain; }
	void set_omnidirectional_radius(float p_radius);
	float get_omnidirectional_radius() const { return settings.omnidirectional_radius; }
	void set_stereo_spread(float p_spread);
	float get_stereo_spread() const { return settings.stereo_spread; }
	void set_stop_on_muted_bus(const StringName &p_bus) { settings.stop_on_muted_bus = p_bus; }
	StringName get_stop_on_muted_bus() const { return settings.stop_on_muted_bus; }
	Vector4 channel_matrix(Vector3 p_position, bool p_spatial) const;
	Ref<AudioStreamPlayback> instantiate_playback() override;
	double get_length() const override;
	bool has_loop() const override;
	bool is_meta_stream() const override { return true; }
	void get_parameter_list(List<Parameter> *r_parameters) override;
};

VARIANT_ENUM_CAST(AudioStreamPanner::Mode);

class AudioStreamPlaybackPanner : public AudioStreamPlayback {
	GDCLASS(AudioStreamPlaybackPanner, AudioStreamPlayback);
	friend class AudioStreamPanner;
	friend class WAudioPlaybackProbe;
	Ref<AudioStreamPanner> panner;
	Ref<AudioStream> source;
	AudioStreamPanner::Settings settings;
	Ref<AudioStreamPlayback> child;
	mutable Mutex parameter_mutex;
	Vector3 position;
	bool listener_active = false;
	SafeFlag mixing;

protected:
	static void _bind_methods() {}

public:
	Vector4 channel_matrix() const;
	bool is_spatialized() const;
	bool is_metadata_only() const override { return child->is_metadata_only(); }
	void start(double p_from_pos = 0.0) override;
	void stop() override;
	bool is_playing() const override;
	int get_loop_count() const override;
	double get_playback_position() const override;
	void seek(double p_time) override;
	int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
	void set_parameter(const StringName &p_name, const Variant &p_value) override;
	Variant get_parameter(const StringName &p_name) const override;
	void tag_used_streams() override;
};
