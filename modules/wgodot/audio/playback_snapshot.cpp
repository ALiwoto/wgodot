// wgodot-changes::file
#include "playback_snapshot.h"
#ifdef TOOLS_ENABLED
#include "core/os/thread.h"
#include "modules/modules_enabled.gen.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/audio/audio_stream_player_internal.h"
#include "scene/resources/audio/audio_stream_polyphonic.h"
#include "servers/audio/audio_server.h"
#ifdef MODULE_AUDIO_GRAPH_ENABLED
#include "modules/audio_graph/audio_stream_group.h"
#endif

void WAudioPlaybackProbe::append(Snapshot &r_snapshot, const Ref<AudioStream> &p_stream,
		const Ref<AudioStreamPlayback> &p_playback, const String &p_path,
		float p_volume, float p_pitch, bool p_pending, bool p_finishing,
		HashSet<ObjectID> &r_ancestors) {
	if (p_stream.is_null() || p_playback.is_null()) {
		r_snapshot.errors.push_back(p_path + ": missing stream or playback");
		return;
	}
	if (r_snapshot.voices.size() >= 4096 || r_ancestors.size() >= 128 || r_ancestors.has(p_playback->get_instance_id())) {
		r_snapshot.errors.push_back(p_path + ": cyclic or oversized playback graph");
		return;
	}
	r_ancestors.insert(p_playback->get_instance_id());
	Voice voice;
	voice.path = p_path;
	voice.stream_path = p_stream->get_path();
	voice.stream_class = p_stream->get_class();
	voice.position = p_playback->get_playback_position();
	voice.duration = p_stream->get_length();
	voice.volume = p_volume;
	voice.pitch = p_pitch;
	voice.loops = p_playback->get_loop_count();
	voice.playing = p_playback->is_playing();
	voice.pending = p_pending;
	voice.finishing = p_finishing;
	voice.leaf = !p_stream->is_meta_stream();
	voice.children_supported = voice.leaf;
	const Variant looping = p_playback->get_parameter(SNAME("looping"));
	voice.looping = looping.get_type() == Variant::BOOL ? bool(looping) : p_stream->has_loop();
	const int parent = r_snapshot.voices.size();
	r_snapshot.voices.push_back(voice);
	if (Ref<AudioStreamPlaybackPolyphonic> polyphonic = p_playback; polyphonic.is_valid()) {
		voice.children_supported = true;
		for (uint32_t i = 0; i < polyphonic->streams.size(); i++) {
			const AudioStreamPlaybackPolyphonic::Stream &child = polyphonic->streams[i];
			if (!child.active.is_set()) {
				continue;
			}
			append(r_snapshot, child.stream, child.stream_playback,
					p_path + "/voice" + itos(i) + ":" + itos(child.id),
					p_volume * Math::db_to_linear(child.volume_db), p_pitch * child.pitch_scale,
					child.pending_play.is_set(), child.finish_request.is_set(), r_ancestors);
		}
	}
#ifdef MODULE_AUDIO_GRAPH_ENABLED
	else if (Ref<AudioStreamPlaybackGroup> group = p_playback; group.is_valid()) {
		voice.children_supported = true;
		voice.volume *= group->playback_gain();
		voice.pitch *= group->pitch;
		voice.delay = group->delay;
		voice.selection = group->index;
		if (group->children_started && group->active) {
			for (int i = 0; i < group->children.size(); i++) {
				const Ref<AudioStreamPlayback> &child = group->children[i];
				if (child.is_null() || !child->is_playing()) {
					continue;
				}
				append(r_snapshot, group->group->streams[i], child, p_path + "/child" + itos(i),
						voice.volume * group->group->stream_volume(i), voice.pitch, false, false, r_ancestors);
			}
		}
	}
#endif
	if (!voice.children_supported) {
		r_snapshot.errors.push_back(p_path + ": unsupported composite stream " + voice.stream_class);
	}
	r_snapshot.voices.write[parent] = voice;
	r_ancestors.erase(p_playback->get_instance_id());
}

WAudioPlaybackProbe::Snapshot WAudioPlaybackProbe::player(Node *p_player) {
	Snapshot snapshot;
	ERR_FAIL_COND_V(!Thread::is_main_thread(), snapshot);
	AudioStreamPlayerInternal *internal = nullptr;
	if (AudioStreamPlayer *player = Object::cast_to<AudioStreamPlayer>(p_player)) {
		internal = player->internal;
	} else if (AudioStreamPlayer2D *player_2d = Object::cast_to<AudioStreamPlayer2D>(p_player)) {
		internal = player_2d->internal;
	} else if (AudioStreamPlayer3D *player_3d = Object::cast_to<AudioStreamPlayer3D>(p_player)) {
		internal = player_3d->internal;
	}
	ERR_FAIL_NULL_V(internal, snapshot);
	HashSet<ObjectID> ancestors;
	AudioServer::get_singleton()->lock();
	for (int i = 0; i < internal->stream_playbacks.size(); i++) {
		append(snapshot, internal->stream, internal->stream_playbacks[i], "playback" + itos(i),
				1.0f, 1.0f, false, false, ancestors);
	}
	AudioServer::get_singleton()->unlock();
	return snapshot;
}
#endif
