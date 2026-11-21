// wgodot-changes::file
#include "playback_snapshot.h"
#ifdef TOOLS_ENABLED
#include "core/io/marshalls.h"
#include "core/os/thread.h"
#include "modules/modules_enabled.gen.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/audio/audio_stream_player_internal.h"
#include "scene/resources/audio/audio_stream_polyphonic.h"
#include "servers/audio/audio_server.h"
#ifdef MODULE_VORBIS_ENABLED
#include "modules/vorbis/audio_stream_ogg_vorbis.h"
#endif
#ifdef MODULE_AUDIO_GRAPH_ENABLED
#include "modules/audio_graph/audio_stream_group.h"
#include "modules/audio_graph/audio_stream_panner.h"
#endif
#ifdef MODULE_INTERACTIVE_MUSIC_ENABLED
#include "modules/interactive_music/audio_stream_interactive.h"
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
	voice.metadata_only = p_playback->is_metadata_only();
#ifdef MODULE_VORBIS_ENABLED
	if (Ref<AudioStreamPlaybackOggVorbis> vorbis = p_playback; vorbis.is_valid()) {
		if (vorbis->info_is_allocated) {
			voice.input_channels = vorbis->info.channels;
			voice.sample_rate = vorbis->info.rate;
		} else if (vorbis->metadata_only && vorbis->vorbis_data.is_valid()) {
			// Metadata-only playback deliberately has no decoder state. Read
			// its identification packet with a separate cursor; never touch the
			// playing cursor or decode waveform data just for an observation.
			Ref<OggPacketSequencePlayback> reader = vorbis->vorbis_data->instantiate_playback();
			ogg_packet *packet = nullptr;
			if (reader->next_ogg_packet(&packet) && packet->bytes >= 30 && memcmp(packet->packet, "\x01vorbis", 7) == 0) {
				voice.input_channels = packet->packet[11];
				voice.sample_rate = decode_uint32(packet->packet + 12);
			} else {
				r_snapshot.errors.push_back(p_path + ": missing Vorbis identification packet");
			}
		}
	}
#endif
	if (AudioServer::get_singleton()->is_metadata_only_enabled() && voice.leaf && !voice.metadata_only) {
		r_snapshot.errors.push_back(p_path + ": metadata-only decoding is unsupported for " + voice.stream_class);
	}
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
					p_pending || child.pending_play.is_set(), p_finishing || child.finish_request.is_set(), r_ancestors);
		}
	}
#ifdef MODULE_AUDIO_GRAPH_ENABLED
	else if (Ref<AudioStreamPlaybackPanner> panner = p_playback; panner.is_valid()) {
		voice.children_supported = true;
		voice.pan_channels = panner->settings.input_channels;
		voice.pan_spatial = panner->is_spatialized();
		const Vector4 matrix = panner->channel_matrix();
		voice.pan_matrix = { matrix.x, matrix.y, matrix.z, matrix.w };
		append(r_snapshot, panner->source, panner->child, p_path + "/stream",
				p_volume, p_pitch, p_pending, p_finishing, r_ancestors);
	}
	else if (Ref<AudioStreamPlaybackGroup> group = p_playback; group.is_valid()) {
		voice.children_supported = true;
		voice.group_mode = group->group->mode;
		voice.group_streams = group->group->streams.size();
		voice.group_loop = group->group->loop;
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
						voice.volume * group->group->stream_volume(i), voice.pitch,
						p_pending || group->delay > 0.0, p_finishing, r_ancestors);
			}
		}
	}
#endif
#ifdef MODULE_INTERACTIVE_MUSIC_ENABLED
	else if (Ref<AudioStreamPlaybackInteractive> interactive = p_playback; interactive.is_valid()) {
		voice.children_supported = true;
		voice.selection = interactive->playback_current;
		for (int i = 0; i < interactive->stream->get_clip_count(); i++) {
			const AudioStreamPlaybackInteractive::State &state = interactive->states[i];
			if (!state.active) {
				continue;
			}
			const int child = r_snapshot.voices.size();
			append(r_snapshot, state.stream, state.playback, p_path + "/clip" + itos(i),
					p_volume * state.fade_volume, p_pitch, p_pending || state.first_mix,
					p_finishing || state.fade_speed < 0.0, r_ancestors);
			if (r_snapshot.voices.size() > child) {
				r_snapshot.voices.write[child].delay += state.fade_wait;
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
	AudioServer *server = AudioServer::get_singleton();
	server->lock();
	for (int i = 0; i < internal->stream_playbacks.size(); i++) {
		const String path = "playback" + itos(i);
		append(snapshot, internal->stream, internal->stream_playbacks[i], path,
				1.0f, 1.0f, false, false, ancestors);
		Mixer mixer;
		mixer.path = path;
		AudioServer::AudioStreamPlaybackListNode *node = server->_find_playback_list_node(internal->stream_playbacks[i]);
		if (node) {
			mixer.state = node->state.load();
			mixer.pitch = node->pitch_scale.get();
			mixer.highshelf_gain = node->highshelf_gain.get();
			mixer.cutoff_hz = node->attenuation_filter_cutoff_hz.get();
			const AudioServer::AudioStreamPlaybackBusDetails *details = node->bus_details.load();
			if (details) {
				for (int bus = 0; bus < AuSC::MAX_BUSES_PER_PLAYBACK; bus++) {
					if (!details->bus_active[bus]) {
						continue;
					}
					Route route;
					route.bus = details->bus[bus];
					for (int channel = 0; channel < AuSC::MAX_CHANNELS_PER_BUS; channel++) {
						route.gains.append_array({ details->volume[bus][channel].left, details->volume[bus][channel].right });
					}
					mixer.routes.push_back(route);
				}
			}
		}
		snapshot.mixers.push_back(mixer);
	}
	server->unlock();
	return snapshot;
}
#endif
