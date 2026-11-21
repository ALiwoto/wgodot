// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "playback_events.h"
#include "core/object/ref_counted.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"

class Node;
class AudioStreamPlayback;
class AudioStreamPlayerInternal;

// Owns the Dummy mixer clock while a scene is held for offline inspection.
// Player updates still use their normal runtime notifications.
class WAudioStepper : public RefCounted {
	GDCLASS(WAudioStepper, RefCounted);
	static WAudioStepper *active;
	static SafeFlag clock_owned;
	static SafeFlag event_thread_error;
	ObjectID root_id;
	Vector<ObjectID> players;
	double fractional_frames = 0.0;
	uint64_t advanced_frames = 0;
	bool stepping = false;
	bool releasing = false;
	double history_origin = 0;
	WAudioPlaybackHistory history;
	double clock_seconds() const;
	void collect(Node *p_node);
	Error update_players();

protected:
	static void _bind_methods();

public:
	static Ref<WAudioStepper> begin(Node *p_root);
	Error advance(double p_seconds);
	void finish();
	Error reset_events();
	static WAudioPlaybackHistory playback_history();
	static void observe(Node *p_node, const AudioStreamPlayerInternal &p_player,
			const Ref<AudioStreamPlayback> &p_playback, const char *p_kind, double p_offset = 0);
	uint64_t get_advanced_frames() const { return advanced_frames; }
	static bool owns(Node *p_node);
	static bool can_process(Node *p_node);
	static bool skip_notification(Node *p_node, int p_notification);
	static void entered(Node *p_node);
	static void exited(Node *p_node);
	~WAudioStepper();
};
#endif
