// wgodot-changes::file
#include "audio_stepper.h"

#ifdef TOOLS_ENABLED
#include "playback_snapshot.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "modules/modules_enabled.gen.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/audio/audio_stream_player_internal.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/audio/audio_stream_polyphonic.h"
#include "servers/audio/audio_driver_dummy.h"
#include "servers/audio/audio_server.h"
#ifdef MODULE_AUDIO_GRAPH_ENABLED
#include "modules/audio_graph/audio_stream_group.h"
#endif

WAudioStepper *WAudioStepper::active = nullptr;
SafeFlag WAudioStepper::clock_owned;
SafeFlag WAudioStepper::event_thread_error;

void WAudioStepper::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("begin", "root"), &WAudioStepper::begin);
	ClassDB::bind_method(D_METHOD("advance", "seconds"), &WAudioStepper::advance);
	ClassDB::bind_method(D_METHOD("get_advanced_frames"), &WAudioStepper::get_advanced_frames);
	ClassDB::bind_method(D_METHOD("finish"), &WAudioStepper::finish);
	ClassDB::bind_method(D_METHOD("reset_events"), &WAudioStepper::reset_events);
	ClassDB::bind_method(D_METHOD("begin_random_frame"), &WAudioStepper::begin_random_frame);
	ClassDB::bind_method(D_METHOD("queue_random_input", "stream", "unit", "used"), &WAudioStepper::queue_random_input);
	ClassDB::bind_method(D_METHOD("queue_range_input", "stream", "parameter", "unit"), &WAudioStepper::queue_range_input);
	ClassDB::bind_method(D_METHOD("end_random_frame"), &WAudioStepper::end_random_frame);
	ClassDB::bind_method(D_METHOD("end_random_inputs"), &WAudioStepper::end_random_inputs);
}

double WAudioStepper::clock_seconds() const {
	return (advanced_frames + fractional_frames) / AudioDriverDummy::get_dummy_singleton()->get_mix_rate();
}

Error WAudioStepper::reset_events() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(stepping || releasing, ERR_BUSY);
	end_random_inputs();
	history = WAudioPlaybackHistory();
	history.enabled = true;
	history_origin = clock_seconds();
	return OK;
}

bool WAudioStepper::observing() {
	if (!clock_owned.is_set()) {
		return false;
	}
	// Never read active, player properties or scene hierarchy on a worker.
	if (!Thread::is_main_thread()) {
		event_thread_error.set();
		return false;
	}
	return active && active->history.enabled;
}

void WAudioStepper::record(Node *p_node, const AudioStreamPlayerInternal &p_player,
		const Ref<AudioStreamPlayback> &p_playback, const Ref<AudioStream> &p_stream,
		const char *p_kind, double p_offset, float p_volume_db, float p_pitch, bool p_mixed) {
	if (!owns(p_node)) {
		return;
	}
	WAudioPlaybackHistory &history = active->history;
	if (history.events.size() >= 4096) {
		if (history.errors.is_empty()) {
			history.errors.push_back("Audio playback event budget exceeded (4096)");
		}
		return;
	}
	WAudioPlaybackEvent event;
	event.kind = p_kind;
#ifdef MODULE_AUDIO_GRAPH_ENABLED
	Ref<AudioStreamPlaybackGroup> group = p_playback;
	if (event.kind == "finished" && group.is_valid() && group->was_instance_limited()) {
		event.kind = "limited";
	}
#endif
	event.player = String(p_node->get_path());
	event.playback_id = uitos(p_playback->get_instance_id());
	event.stream = p_stream.is_valid() ? p_stream->get_path() : String();
	event.bus = p_player.get_bus();
	// Completion is observed at the end of this manual advance, separately
	// from the precise gameplay request clock. Mixer lookahead is not a trigger.
	event.seconds = (p_mixed ? active->mixing_until : active->clock_seconds()) - active->history_origin;
	event.offset = p_offset;
	event.volume_db = p_player.volume_db + p_volume_db;
	event.pitch = p_player.pitch_scale * p_pitch;
	history.events.push_back(event);
}

void WAudioStepper::observe(Node *p_node, const AudioStreamPlayerInternal &p_player,
		const Ref<AudioStreamPlayback> &p_playback, const char *p_kind, double p_offset) {
	if (!observing() || !owns(p_node)) {
		return;
	}
	if (Ref<AudioStreamPlaybackPolyphonic> polyphonic = p_playback; polyphonic.is_valid()) {
		// The container is not a cue request. Its children retain their own
		// stream identity and playback ID, including on parent interruption.
		if (String(p_kind) != "play") {
			for (const AudioStreamPlaybackPolyphonic::Stream &child : polyphonic->streams) {
				if (child.active.is_set() && !child.finish_request.is_set()) {
					record(p_node, p_player, child.stream_playback, child.stream, p_kind,
							0, child.volume_db, child.pitch_scale);
				}
			}
		}
		return;
	}
	record(p_node, p_player, p_playback, p_player.stream, p_kind, p_offset, 0, 1);
}

void WAudioStepper::observe_polyphonic(AudioStreamPlaybackPolyphonic *p_parent,
		const Ref<AudioStreamPlayback> &p_playback, const Ref<AudioStream> &p_stream,
		const char *p_kind, double p_offset, float p_volume_db, float p_pitch, bool p_mixed) {
	if (!observing()) {
		return;
	}
	// Resolve current ownership only. Do not retain nodes or audio resources
	// for inspection, or require a new play after beginning/resetting capture.
	for (ObjectID id : active->players) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
		AudioStreamPlayerInternal *internal = WAudioPlaybackProbe::player_internal(node);
		if (!internal) {
			continue;
		}
		for (const Ref<AudioStreamPlayback> &playback : internal->stream_playbacks) {
			if (playback.ptr() == p_parent) {
				record(node, *internal, p_playback, p_stream, p_kind, p_offset,
						p_volume_db, p_pitch, p_mixed);
				return;
			}
		}
	}
}

WAudioPlaybackHistory WAudioStepper::playback_history() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), WAudioPlaybackHistory());
	if (!active) {
		return WAudioPlaybackHistory();
	}
	WAudioPlaybackHistory result = active->history;
	result.seconds = active->clock_seconds() - active->history_origin;
	if (event_thread_error.is_set()) {
		result.errors.push_back("Audio playback event dispatched outside the main inspection thread");
	}
	return result;
}

bool WAudioStepper::owns(Node *p_node) {
	if (!active || !p_node) {
		return false;
	}
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(active->root_id));
	return root && (root == p_node || root->is_ancestor_of(p_node));
}

bool WAudioStepper::can_process(Node *p_node) {
	if (!owns(p_node) || active->releasing) {
		return p_node->can_process();
	}
	// A held SceneTree is the inspection clock, not an authored audio pause.
	// Keep disabled/when-paused process modes and explicit stream pauses intact.
	return p_node->can_process_with_scene_pause(false);
}

bool WAudioStepper::skip_notification(Node *p_node, int p_notification) {
	if (p_notification != Node::NOTIFICATION_INTERNAL_PROCESS && p_notification != Node::NOTIFICATION_INTERNAL_PHYSICS_PROCESS) {
		return false;
	}
	// Process groups can dispatch these notifications on worker threads. They
	// must not dereference the main-thread scope or traverse its scene nodes.
	if (!Thread::is_main_thread()) {
		return clock_owned.is_set();
	}
	return owns(p_node) && !active->stepping;
}

void WAudioStepper::entered(Node *p_node) {
	if (owns(p_node)) {
		active->players.push_back(p_node->get_instance_id());
	}
}

void WAudioStepper::exited(Node *p_node) {
	if (active) {
		active->players.erase(p_node->get_instance_id());
	}
}

void WAudioStepper::collect(Node *p_node) {
	if (Object::cast_to<AudioStreamPlayer>(p_node) || Object::cast_to<AudioStreamPlayer2D>(p_node) || Object::cast_to<AudioStreamPlayer3D>(p_node)) {
		players.push_back(p_node->get_instance_id());
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect(p_node->get_child(i));
	}
}

Ref<WAudioStepper> WAudioStepper::begin(Node *p_root) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), Ref<WAudioStepper>());
	ERR_FAIL_NULL_V(p_root, Ref<WAudioStepper>());
	ERR_FAIL_COND_V(!p_root->is_inside_tree(), Ref<WAudioStepper>());
	ERR_FAIL_COND_V_MSG(p_root != p_root->get_tree()->get_root(), Ref<WAudioStepper>(), "The global audio clock requires the complete SceneTree root.");
	ERR_FAIL_COND_V_MSG(active, Ref<WAudioStepper>(), "Another controller owns the audio clock.");
	ERR_FAIL_COND_V_MSG(p_root->get_tree()->is_paused(), Ref<WAudioStepper>(), "Begin audio stepping before holding the SceneTree.");
	ERR_FAIL_COND_V_MSG(AudioServer::get_singleton()->get_driver_name() != "Dummy", Ref<WAudioStepper>(), "Audio stepping requires the Dummy driver.");
	Ref<WAudioStepper> result;
	result.instantiate();
	result->root_id = p_root->get_instance_id();
	result->collect(p_root);
	AudioDriverDummy::get_dummy_singleton()->set_manual_advance(true);
	active = result.ptr();
	event_thread_error.clear();
	clock_owned.set();
	return result;
}

Error WAudioStepper::update_players() {
	// Finished callbacks may remove players or add new ones. Do not retain
	// raw nodes or an iterator across those ordinary gameplay callbacks.
	Vector<ObjectID> pending;
	for (ObjectID id : players) {
		pending.push_back(id);
	}
	for (ObjectID id : pending) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (!owns(node) || !can_process(node)) {
			continue;
		}
		AudioStreamPlayer3D *spatial = Object::cast_to<AudioStreamPlayer3D>(node);
		ERR_FAIL_COND_V_MSG(spatial && spatial->get_doppler_tracking() != AudioStreamPlayer3D::DOPPLER_TRACKING_DISABLED,
				ERR_UNAVAILABLE, "Offline audio does not yet own the Doppler velocity-history clock.");
		if (node->is_physics_processing_internal()) {
			node->notification(Node::NOTIFICATION_INTERNAL_PHYSICS_PROCESS);
		} else if (node->is_processing_internal()) {
			node->notification(Node::NOTIFICATION_INTERNAL_PROCESS);
		}
	}
	return OK;
}

Error WAudioStepper::advance(double p_seconds) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(stepping, ERR_BUSY);
	ERR_FAIL_COND_V(!Math::is_finite(p_seconds) || p_seconds < 0.0 || p_seconds > 60.0, ERR_INVALID_PARAMETER);
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(root_id));
	ERR_FAIL_COND_V(!root || !root->is_inside_tree() || !root->get_tree()->is_paused(), ERR_UNCONFIGURED);
	Ref<WAudioStepper> keep_alive(this);
	stepping = true;
	const Error player_error = update_players();
	if (player_error != OK) {
		stepping = false;
		return player_error;
	}
	// Callbacks can release the clock; do not advance a subsequently restored driver.
	if (active != this) {
		stepping = false;
		return ERR_UNCONFIGURED;
	}
	AudioDriverDummy *driver = AudioDriverDummy::get_dummy_singleton();
	const double exact_frames = fractional_frames + p_seconds * driver->get_mix_rate();
	const int frames = int(Math::floor(exact_frames));
	mixing_until = clock_seconds() + p_seconds;
	const Error error = driver->advance_manual(frames);
	if (error == OK) {
		fractional_frames = exact_frames - frames;
		advanced_frames += frames;
	}
	stepping = false;
	return error;
}

void WAudioStepper::finish() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (active == this) {
		if (releasing) {
			return;
		}
		end_random_inputs();
		releasing = true;
		const Vector<ObjectID> pending = players;
		for (ObjectID id : pending) {
			Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
			if (node && node->is_inside_tree() && !node->can_process()) {
				node->notification(Node::NOTIFICATION_PAUSED);
			}
		}
		active = nullptr;
		AudioDriverDummy::get_dummy_singleton()->set_manual_advance(false);
		clock_owned.clear();
		releasing = false;
	}
	players.clear();
}

WAudioStepper::~WAudioStepper() {
	finish();
}
#endif
