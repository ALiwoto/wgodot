// wgodot-changes::file
#include "audio_stream_panner.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

void AudioStreamPanner::set_stream(const Ref<AudioStream> &p_stream) {
	ERR_FAIL_COND(p_stream.ptr() == this);
	stream = p_stream;
	emit_changed();
}

void AudioStreamPanner::set_input_channels(int p_channels) {
	ERR_FAIL_COND(p_channels != 1 && p_channels != 2);
	settings.input_channels = p_channels;
}

void AudioStreamPanner::set_mode(Mode p_mode) {
	ERR_FAIL_COND(p_mode < DISABLED || p_mode > EQUAL_POWER);
	settings.mode = p_mode;
}

void AudioStreamPanner::set_mono_gain(float p_gain) {
	ERR_FAIL_COND(!Math::is_finite(p_gain) || p_gain < 0);
	settings.mono_gain = p_gain;
}

void AudioStreamPanner::set_omnidirectional_radius(float p_radius) {
	ERR_FAIL_COND(!Math::is_finite(p_radius) || p_radius < 0);
	settings.omnidirectional_radius = p_radius;
}

void AudioStreamPanner::set_stereo_spread(float p_spread) {
	ERR_FAIL_COND(!Math::is_finite(p_spread) || p_spread < 0);
	settings.stereo_spread = p_spread;
}

static Vector2 azimuth_gains(float angle, AudioStreamPanner::Mode p_mode, float p_omni) {
	if (angle < 0) {
		angle += 360.0f;
	} else if (angle > 360.0f) {
		angle -= 360.0f;
	}
	const bool previous_right = angle > 90.0f && angle <= 270.0f;
	const float previous = previous_right ? 90.0f : (angle > 270.0f ? 270.0f : -90.0f);
	const float fraction = (angle - previous) / 180.0f;
	float previous_gain = 1.0f - fraction;
	float next_gain = fraction;
	if (p_mode == AudioStreamPanner::EQUAL_POWER) {
		previous_gain = Math::cos(fraction * float(Math::PI) * 0.5f);
		next_gain = Math::sin(fraction * float(Math::PI) * 0.5f);
	}
	Vector2 gains = previous_right ? Vector2(next_gain, previous_gain) : Vector2(previous_gain, next_gain);
	return gains.lerp(Vector2(0.5f, 0.5f), p_omni);
}

static Vector4 panning_matrix(const AudioStreamPanner::Settings &p_settings, Vector3 p_position, bool p_spatial) {
	if (p_settings.mode == AudioStreamPanner::DISABLED || !p_spatial) {
		return p_settings.input_channels == 1 ? Vector4(p_settings.mono_gain, 0, p_settings.mono_gain, 0) : Vector4(1, 0, 0, 1);
	}
	float omni = 0.0f;
	const float distance = p_position.length();
	if (p_settings.omnidirectional_radius > distance) {
		const float relative = distance / p_settings.omnidirectional_radius;
		omni = 1.0f - relative * relative;
	}
	const float azimuth = Math::rad_to_deg(Math::atan2(p_position.x, -p_position.z));
	if (p_settings.input_channels == 1) {
		const Vector2 gains = azimuth_gains(azimuth, p_settings.mode, omni);
		return Vector4(gains.x, 0, gains.y, 0);
	}
	// Speaker panning uses angular width, not a pair of world-space emitters.
	// In particular, elevation must not collapse a stereo sound to mono.
	float left_azimuth = 90.0f;
	float right_azimuth = 270.0f;
	if (distance > 1e-6f) {
		const float offset = Math::rad_to_deg(Math::atan(0.5f * p_settings.stereo_spread / distance));
		left_azimuth = azimuth - offset;
		right_azimuth = azimuth + offset;
	}
	const Vector2 left = azimuth_gains(left_azimuth, p_settings.mode, omni);
	const Vector2 right = azimuth_gains(right_azimuth, p_settings.mode, omni);
	return Vector4(left.x, right.x, left.y, right.y);
}

Vector4 AudioStreamPanner::channel_matrix(Vector3 p_position, bool p_spatial) const {
	return panning_matrix(settings, p_position, p_spatial);
}

Ref<AudioStreamPlayback> AudioStreamPanner::instantiate_playback() {
	ERR_FAIL_COND_V(stream.is_null(), Ref<AudioStreamPlayback>());
	Ref<AudioStreamPlaybackPanner> playback;
	playback.instantiate();
	playback->panner = Ref<AudioStreamPanner>(this);
	// Authored settings belong to this playback. Main-thread resource edits
	// cannot race an audio-thread mix or silently change a running voice.
	playback->settings = settings;
	playback->source = stream;
	playback->child = stream->instantiate_playback();
	ERR_FAIL_COND_V(playback->child.is_null(), Ref<AudioStreamPlayback>());
	return playback;
}

double AudioStreamPanner::get_length() const {
	return stream.is_valid() ? stream->get_length() : 0.0;
}

bool AudioStreamPanner::has_loop() const {
	return stream.is_valid() && stream->has_loop();
}

void AudioStreamPanner::get_parameter_list(List<Parameter> *r_parameters) {
	if (stream.is_valid()) {
		stream->get_parameter_list(r_parameters);
	}
}

Vector4 AudioStreamPlaybackPanner::channel_matrix() const {
	MutexLock lock(parameter_mutex);
	return panning_matrix(settings, position, listener_active);
}

bool AudioStreamPlaybackPanner::is_spatialized() const {
	MutexLock lock(parameter_mutex);
	return listener_active && settings.mode != AudioStreamPanner::DISABLED;
}

void AudioStreamPlaybackPanner::start(double p_from_pos) {
	child->start(p_from_pos);
	mixing.set();
}
void AudioStreamPlaybackPanner::stop() {
	child->stop();
	mixing.clear();
}
bool AudioStreamPlaybackPanner::is_playing() const { return mixing.is_set(); }
int AudioStreamPlaybackPanner::get_loop_count() const { return child->get_loop_count(); }
double AudioStreamPlaybackPanner::get_playback_position() const { return child->get_playback_position(); }
void AudioStreamPlaybackPanner::seek(double p_time) { child->seek(p_time); }

int AudioStreamPlaybackPanner::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (!mixing.is_set()) {
		return 0;
	}
	if (settings.stop_on_muted_bus != StringName()) {
		AudioServer *server = AudioServer::get_singleton();
		const int bus = server->get_bus_index(settings.stop_on_muted_bus);
		if (bus < 0) {
			stop();
			ERR_FAIL_V_MSG(0, "Audio stream's stop-on-mute bus does not exist: " + String(settings.stop_on_muted_bus));
		}
		if (server->is_bus_mute(bus)) {
			// The selection/start has already happened. Terminate this leaf
			// before further mixing; unmuting must not revive the one-shot.
			stop();
			return 0;
		}
	}
	const Vector4 matrix = channel_matrix();
	const int mixed = child->mix(p_buffer, p_rate_scale, p_frames);
	if (mixed < p_frames) {
		mixing.clear();
	}
	if (!child->is_metadata_only()) {
		for (int i = 0; i < mixed; i++) {
			const AudioFrame input = p_buffer[i];
			p_buffer[i] = AudioFrame(matrix.x * input.left + matrix.y * input.right,
					matrix.z * input.left + matrix.w * input.right);
		}
	}
	return mixed;
}

void AudioStreamPlaybackPanner::set_parameter(const StringName &p_name, const Variant &p_value) {
	if (p_name == SNAME("listener_frame")) {
		ERR_FAIL_COND(p_value.get_type() != Variant::NIL && p_value.get_type() != Variant::TRANSFORM3D);
		const bool active = p_value.get_type() == Variant::TRANSFORM3D;
		const Transform3D frame = active ? Transform3D(p_value) : Transform3D();
		ERR_FAIL_COND(!frame.is_finite());
		MutexLock lock(parameter_mutex);
		position = frame.origin;
		listener_active = active;
	}
	child->set_parameter(p_name, p_value);
}

Variant AudioStreamPlaybackPanner::get_parameter(const StringName &p_name) const {
	return child->get_parameter(p_name);
}

void AudioStreamPlaybackPanner::tag_used_streams() {
	panner->tag_used(get_playback_position());
	child->tag_used_streams();
}

void AudioStreamPanner::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_stream", "stream"), &AudioStreamPanner::set_stream);
	ClassDB::bind_method(D_METHOD("get_stream"), &AudioStreamPanner::get_stream);
	ClassDB::bind_method(D_METHOD("set_input_channels", "channels"), &AudioStreamPanner::set_input_channels);
	ClassDB::bind_method(D_METHOD("get_input_channels"), &AudioStreamPanner::get_input_channels);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &AudioStreamPanner::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &AudioStreamPanner::get_mode);
	ClassDB::bind_method(D_METHOD("set_mono_gain", "gain"), &AudioStreamPanner::set_mono_gain);
	ClassDB::bind_method(D_METHOD("get_mono_gain"), &AudioStreamPanner::get_mono_gain);
	ClassDB::bind_method(D_METHOD("set_omnidirectional_radius", "radius"), &AudioStreamPanner::set_omnidirectional_radius);
	ClassDB::bind_method(D_METHOD("get_omnidirectional_radius"), &AudioStreamPanner::get_omnidirectional_radius);
	ClassDB::bind_method(D_METHOD("set_stereo_spread", "spread"), &AudioStreamPanner::set_stereo_spread);
	ClassDB::bind_method(D_METHOD("get_stereo_spread"), &AudioStreamPanner::get_stereo_spread);
	ClassDB::bind_method(D_METHOD("set_stop_on_muted_bus", "bus"), &AudioStreamPanner::set_stop_on_muted_bus);
	ClassDB::bind_method(D_METHOD("get_stop_on_muted_bus"), &AudioStreamPanner::get_stop_on_muted_bus);
	ClassDB::bind_method(D_METHOD("channel_matrix", "position", "spatial"), &AudioStreamPanner::channel_matrix);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream", "get_stream");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_channels", PROPERTY_HINT_RANGE, "1,2,1"), "set_input_channels", "get_input_channels");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Disabled,Linear,Equal Power"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mono_gain", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_mono_gain", "get_mono_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "omnidirectional_radius", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_omnidirectional_radius", "get_omnidirectional_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stereo_spread", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_stereo_spread", "get_stereo_spread");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "stop_on_muted_bus"), "set_stop_on_muted_bus", "get_stop_on_muted_bus");
	BIND_ENUM_CONSTANT(DISABLED);
	BIND_ENUM_CONSTANT(LINEAR);
	BIND_ENUM_CONSTANT(EQUAL_POWER);
}
