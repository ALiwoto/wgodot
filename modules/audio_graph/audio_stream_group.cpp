// wgodot-changes::file
#include "audio_stream_group.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

void AudioStreamGroup::set_streams(const TypedArray<AudioStream> &p_value) {
	MutexLock lock(selection_mutex);
	streams = p_value;
	used.resize(streams.size());
	used.fill(0);
	emit_changed();
}

void AudioStreamGroup::set_weights(const Vector<float> &p_value) {
	MutexLock lock(selection_mutex);
	weights = p_value;
	used.fill(0);
}

float AudioStreamGroup::stream_volume(int p_index) const {
	return p_index < volumes.size() ? volumes[p_index] : 1.0f;
}

int AudioStreamGroup::choose_index() {
	MutexLock lock(selection_mutex);
	double total = 0.0;
	for (int i = 0; i < streams.size(); i++) {
		if (mode != SHUFFLE || !used[i]) {
			total += MAX(i < weights.size() ? weights[i] : 1.0f, 0.0f);
		}
	}
	if (total <= 0.0) {
		used.fill(0);
		for (int i = 0; i < streams.size(); i++) {
			total += MAX(i < weights.size() ? weights[i] : 1.0f, 0.0f);
		}
	}
	if (total <= 0.0) {
		return -1;
	}
	double choice = Math::randd() * total;
	int selected = -1;
	for (int i = 0; i < streams.size(); i++) {
		const float weight = i < weights.size() ? weights[i] : 1.0f;
		if (weight <= 0.0f || (mode == SHUFFLE && used[i])) {
			continue;
		}
		selected = i;
		choice -= weight;
		if (choice < 0.0) {
			break;
		}
	}
	if (mode == SHUFFLE) {
		used.write[selected] = 1;
		bool remaining = false;
		for (int i = 0; i < streams.size(); i++) {
			if (!used[i] && (i < weights.size() ? weights[i] : 1.0f) > 0.0f) {
				remaining = true;
				break;
			}
		}
		if (!remaining) {
			used.fill(0);
			used.write[selected] = 1;
		}
	}
	return selected;
}

Ref<AudioStreamPlayback> AudioStreamGroup::instantiate_playback() {
	Ref<AudioStreamPlaybackGroup> playback;
	playback.instantiate();
	playback->group = Ref<AudioStreamGroup>(this);
	playback->children.resize(streams.size());
	for (int i = 0; i < streams.size(); i++) {
		Ref<AudioStream> child = streams[i];
		if (child.is_valid()) {
			playback->children.write[i] = child->instantiate_playback();
		}
	}
	return playback;
}

double AudioStreamGroup::get_length() const {
	return scaled_length(1.0);
}

double AudioStreamGroup::scaled_length(double p_rate) const {
	double length = 0.0;
	const double rate = p_rate * MAX(MIN(pitch_range.x, pitch_range.y), 0.001f);
	for (int i = 0; i < streams.size(); i++) {
		Ref<AudioStream> child = streams[i];
		if (child.is_valid()) {
			Ref<AudioStreamGroup> child_group = child;
			const double duration = child_group.is_valid() ? child_group->scaled_length(rate) : child->get_length() / rate;
			length = mode == SEQUENTIAL ? length + duration : MAX(length, duration);
		}
	}
	return MAX(delay_range.x, delay_range.y) / (delay_uses_rate_scale ? p_rate : 1.0) + length;
}

bool AudioStreamGroup::has_loop() const {
	if (loop) {
		return true;
	}
	for (int i = 0; i < streams.size(); i++) {
		Ref<AudioStream> child = streams[i];
		if (child.is_valid() && child->has_loop()) {
			return true;
		}
	}
	return false;
}

void AudioStreamGroup::get_parameter_list(List<Parameter> *r_parameters) {
	HashSet<StringName> names;
	for (const Parameter &parameter : *r_parameters) {
		names.insert(parameter.property.name);
	}
	if (mode == SWITCH && !switch_parameter.is_empty() && !names.has(switch_parameter)) {
		r_parameters->push_back(Parameter(PropertyInfo(Variant::INT, switch_parameter), default_index - switch_offset));
		names.insert(switch_parameter);
	}
	if (!volume_parameter.is_empty() && !names.has(volume_parameter)) {
		r_parameters->push_back(Parameter(PropertyInfo(Variant::FLOAT, volume_parameter), 0.0));
		names.insert(volume_parameter);
	}
	for (int i = 0; i < streams.size(); i++) {
		Ref<AudioStream> child = streams[i];
		if (child.is_null()) {
			continue;
		}
		List<Parameter> child_parameters;
		child->get_parameter_list(&child_parameters);
		for (const Parameter &parameter : child_parameters) {
			if (!names.has(parameter.property.name)) {
				r_parameters->push_back(parameter);
				names.insert(parameter.property.name);
			}
		}
	}
}

void AudioStreamPlaybackGroup::start_child(int p_index) {
	if (p_index < 0 || p_index >= children.size() || children[p_index].is_null()) {
		return;
	}
	Ref<AudioStreamPlaybackGroup> child_group = children[p_index];
	if (seeking && child_group.is_valid() && child_group->initialized) {
		child_group->reset_for_seek();
	} else {
		children[p_index]->start(0.0);
	}
}

void AudioStreamPlaybackGroup::start_children() {
	children_started = true;
	cycle_frames = 0;
	if (group->mode == AudioStreamGroup::PARALLEL) {
		for (int i = 0; i < children.size(); i++) {
			start_child(i);
		}
	} else {
		if (group->mode == AudioStreamGroup::SEQUENTIAL) {
			index = 0;
		}
		start_child(index);
	}
}

void AudioStreamPlaybackGroup::start(double p_from_pos) {
	stop();
	position = 0.0;
	initialized = true;
	seeking = false;
	loops = 0;
	volume = Math::random(group->volume_range.x, group->volume_range.y);
	pitch = MAX(Math::random(group->pitch_range.x, group->pitch_range.y), 0.001f);
	delay_duration = MAX(Math::random(group->delay_range.x, group->delay_range.y), 0.0f);
	delay = delay_duration;
	if (group->mode == AudioStreamGroup::RANDOM || group->mode == AudioStreamGroup::SHUFFLE) {
		index = group->choose_index();
	} else if (group->mode == AudioStreamGroup::SWITCH) {
		MutexLock lock(parameter_mutex);
		const Variant *parameter = parameters.getptr(group->switch_parameter);
		index = parameter ? int(*parameter) + group->switch_offset : group->default_index;
		if (index < 0 || index >= children.size()) {
			index = group->default_index;
		}
	} else {
		index = 0;
	}
	active = true;
	children_started = false;
	if (p_from_pos > 0.0) {
		// A new play samples fresh choices, even when it starts at an offset.
		advance_to(p_from_pos);
	} else if (delay <= 0.0) {
		start_children();
	}
}

void AudioStreamPlaybackGroup::stop() {
	active = false;
	for (const Ref<AudioStreamPlayback> &child : children) {
		if (child.is_valid()) {
			child->stop();
		}
	}
}

void AudioStreamPlaybackGroup::reset_for_seek() {
	stop();
	active = true;
	seeking = true;
	position = 0.0;
	delay = delay_duration;
	loops = 0;
	children_started = false;
}

void AudioStreamPlaybackGroup::seek(double p_time) {
	if (!initialized) {
		start(p_time);
		return;
	}
	// Replay actual child lengths: random choices and delays make the resource's
	// maximum duration unsuitable for locating a position within a sequence.
	reset_for_seek();
	advance_to(p_time);
	finish_seek();
}

void AudioStreamPlaybackGroup::finish_seek() {
	seeking = false;
	for (const Ref<AudioStreamPlayback> &child : children) {
		Ref<AudioStreamPlaybackGroup> child_group = child;
		if (child_group.is_valid()) {
			child_group->finish_seek();
		}
	}
}

void AudioStreamPlaybackGroup::advance_to(double p_time) {
	AudioFrame discard[256];
	const double target = MAX(p_time, 0.0);
	while (active && position < target) {
		const int frames = MIN(256, int(Math::ceil((target - position) * AudioServer::get_singleton()->get_mix_rate())));
		if (mix(discard, 1.0f, frames) == 0) {
			break;
		}
	}
}

int AudioStreamPlaybackGroup::mix_children(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (group->mode == AudioStreamGroup::SWITCH) {
		int selected = group->default_index;
		{
			MutexLock lock(parameter_mutex);
			const Variant *parameter = parameters.getptr(group->switch_parameter);
			if (parameter) {
				selected = int(*parameter) + group->switch_offset;
			}
		}
		if (selected < 0 || selected >= children.size()) {
			selected = group->default_index;
		}
		if (selected != index) {
			if (index >= 0 && index < children.size() && children[index].is_valid()) {
				children[index]->stop();
			}
			index = selected;
			start_child(index);
		}
	}
	int mixed = 0;
	if (group->mode == AudioStreamGroup::PARALLEL) {
		AudioFrame buffer[256];
		for (int i = 0; i < children.size(); i++) {
			if (children[i].is_null() || !children[i]->is_playing()) {
				continue;
			}
			int offset = 0;
			while (offset < p_frames) {
				const int count = MIN(p_frames - offset, 256);
				const int frames = children[i]->mix(buffer, p_rate_scale * pitch, count);
				for (int frame = 0; frame < frames; frame++) {
					p_buffer[offset + frame] += buffer[frame] * group->stream_volume(i);
				}
				offset += frames;
				if (frames < count) {
					break;
				}
			}
			mixed = MAX(mixed, offset);
		}
	} else {
		while (index >= 0 && index < children.size() && mixed < p_frames) {
			Ref<AudioStreamPlayback> child = children[index];
			const int count = p_frames - mixed;
			const int frames = child.is_valid() && child->is_playing() ? child->mix(p_buffer + mixed, p_rate_scale * pitch, count) : 0;
			for (int frame = 0; frame < frames; frame++) {
				p_buffer[mixed + frame] *= group->stream_volume(index);
			}
			mixed += frames;
			if (frames == count || group->mode != AudioStreamGroup::SEQUENTIAL) {
				break;
			}
			index++;
			start_child(index);
		}
	}
	return mixed;
}

int AudioStreamPlaybackGroup::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	for (int i = 0; i < p_frames; i++) {
		p_buffer[i] = AudioFrame();
	}
	if (!active) {
		return 0;
	}
	const double seconds_per_frame = double(p_rate_scale) / AudioServer::get_singleton()->get_mix_rate();
	int mixed = 0;
	if (delay > 0.0) {
		const double delay_step = group->delay_uses_rate_scale ? seconds_per_frame : 1.0 / AudioServer::get_singleton()->get_mix_rate();
		mixed = MIN(p_frames, int(Math::ceil(delay / delay_step)));
		delay = MAX(delay - mixed * delay_step, 0.0);
	}
	if (mixed < p_frames) {
		if (!children_started) {
			start_children();
		}
		while (mixed < p_frames) {
			const int frames = mix_children(p_buffer + mixed, p_rate_scale, p_frames - mixed);
			mixed += frames;
			cycle_frames += frames;
			if (mixed == p_frames) {
				break;
			}
			if (!group->loop || cycle_frames == 0) {
				active = false;
				break;
			}
			loops++;
			start_children();
		}
	}
	float gain = volume;
	if (group->volume_parameter_curve.is_valid()) {
		MutexLock lock(parameter_mutex);
		const Variant *parameter = parameters.getptr(group->volume_parameter);
		const double input = parameter ? double(*parameter) : 0.0;
		const double response = group->volume_parameter_curve->sample(input);
		gain *= group->volume_parameter_db ? Math::db_to_linear(response) : response;
		if (group->volume_parameter_cutoff >= 0.0 && input > group->volume_parameter_cutoff) {
			gain = 0.0;
		}
	}
	for (int i = 0; i < mixed; i++) {
		p_buffer[i] *= gain;
	}
	position += mixed * seconds_per_frame;
	return mixed;
}

void AudioStreamPlaybackGroup::set_parameter(const StringName &p_name, const Variant &p_value) {
	{
		MutexLock lock(parameter_mutex);
		parameters[p_name] = p_value;
	}
	for (const Ref<AudioStreamPlayback> &child : children) {
		if (child.is_valid()) {
			child->set_parameter(p_name, p_value);
		}
	}
}

Variant AudioStreamPlaybackGroup::get_parameter(const StringName &p_name) const {
	MutexLock lock(parameter_mutex);
	const Variant *value = parameters.getptr(p_name);
	return value ? *value : Variant();
}

void AudioStreamPlaybackGroup::tag_used_streams() {
	group->tag_used(position);
	for (const Ref<AudioStreamPlayback> &child : children) {
		if (child.is_valid() && child->is_playing()) {
			child->tag_used_streams();
		}
	}
}

void AudioStreamGroup::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &AudioStreamGroup::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &AudioStreamGroup::get_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Parallel,Sequential,Random,Shuffle,Switch"), "set_mode", "get_mode");
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &AudioStreamGroup::set_streams);
	ClassDB::bind_method(D_METHOD("get_streams"), &AudioStreamGroup::get_streams);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_ARRAY_TYPE, "AudioStream"), "set_streams", "get_streams");
	ClassDB::bind_method(D_METHOD("set_weights", "weights"), &AudioStreamGroup::set_weights);
	ClassDB::bind_method(D_METHOD("get_weights"), &AudioStreamGroup::get_weights);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "weights"), "set_weights", "get_weights");
	ClassDB::bind_method(D_METHOD("set_volumes", "volumes"), &AudioStreamGroup::set_volumes);
	ClassDB::bind_method(D_METHOD("get_volumes"), &AudioStreamGroup::get_volumes);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "volumes"), "set_volumes", "get_volumes");
	ClassDB::bind_method(D_METHOD("set_volume_range", "range"), &AudioStreamGroup::set_volume_range);
	ClassDB::bind_method(D_METHOD("get_volume_range"), &AudioStreamGroup::get_volume_range);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "volume_range"), "set_volume_range", "get_volume_range");
	ClassDB::bind_method(D_METHOD("set_pitch_range", "range"), &AudioStreamGroup::set_pitch_range);
	ClassDB::bind_method(D_METHOD("get_pitch_range"), &AudioStreamGroup::get_pitch_range);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pitch_range"), "set_pitch_range", "get_pitch_range");
	ClassDB::bind_method(D_METHOD("set_delay_range", "range"), &AudioStreamGroup::set_delay_range);
	ClassDB::bind_method(D_METHOD("get_delay_range"), &AudioStreamGroup::get_delay_range);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "delay_range"), "set_delay_range", "get_delay_range");
	ClassDB::bind_method(D_METHOD("set_delay_uses_rate_scale", "enabled"), &AudioStreamGroup::set_delay_uses_rate_scale);
	ClassDB::bind_method(D_METHOD("get_delay_uses_rate_scale"), &AudioStreamGroup::get_delay_uses_rate_scale);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "delay_uses_rate_scale"), "set_delay_uses_rate_scale", "get_delay_uses_rate_scale");
	ClassDB::bind_method(D_METHOD("set_loop", "loop"), &AudioStreamGroup::set_loop);
	ClassDB::bind_method(D_METHOD("get_loop"), &AudioStreamGroup::get_loop);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "get_loop");
	ClassDB::bind_method(D_METHOD("set_switch_parameter", "parameter"), &AudioStreamGroup::set_switch_parameter);
	ClassDB::bind_method(D_METHOD("get_switch_parameter"), &AudioStreamGroup::get_switch_parameter);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "switch_parameter"), "set_switch_parameter", "get_switch_parameter");
	ClassDB::bind_method(D_METHOD("set_switch_offset", "offset"), &AudioStreamGroup::set_switch_offset);
	ClassDB::bind_method(D_METHOD("get_switch_offset"), &AudioStreamGroup::get_switch_offset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "switch_offset"), "set_switch_offset", "get_switch_offset");
	ClassDB::bind_method(D_METHOD("set_default_index", "index"), &AudioStreamGroup::set_default_index);
	ClassDB::bind_method(D_METHOD("get_default_index"), &AudioStreamGroup::get_default_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "default_index"), "set_default_index", "get_default_index");
	ClassDB::bind_method(D_METHOD("set_volume_parameter", "parameter"), &AudioStreamGroup::set_volume_parameter);
	ClassDB::bind_method(D_METHOD("get_volume_parameter"), &AudioStreamGroup::get_volume_parameter);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "volume_parameter"), "set_volume_parameter", "get_volume_parameter");
	ClassDB::bind_method(D_METHOD("set_volume_parameter_curve", "curve"), &AudioStreamGroup::set_volume_parameter_curve);
	ClassDB::bind_method(D_METHOD("get_volume_parameter_curve"), &AudioStreamGroup::get_volume_parameter_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "volume_parameter_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_volume_parameter_curve", "get_volume_parameter_curve");
	ClassDB::bind_method(D_METHOD("set_volume_parameter_db", "enabled"), &AudioStreamGroup::set_volume_parameter_db);
	ClassDB::bind_method(D_METHOD("get_volume_parameter_db"), &AudioStreamGroup::get_volume_parameter_db);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "volume_parameter_db"), "set_volume_parameter_db", "get_volume_parameter_db");
	ClassDB::bind_method(D_METHOD("set_volume_parameter_cutoff", "cutoff"), &AudioStreamGroup::set_volume_parameter_cutoff);
	ClassDB::bind_method(D_METHOD("get_volume_parameter_cutoff"), &AudioStreamGroup::get_volume_parameter_cutoff);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_parameter_cutoff"), "set_volume_parameter_cutoff", "get_volume_parameter_cutoff");
	BIND_ENUM_CONSTANT(PARALLEL);
	BIND_ENUM_CONSTANT(SEQUENTIAL);
	BIND_ENUM_CONSTANT(RANDOM);
	BIND_ENUM_CONSTANT(SHUFFLE);
	BIND_ENUM_CONSTANT(SWITCH);
}
