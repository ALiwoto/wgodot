// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "playback_events.h"
#include "core/object/ref_counted.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "core/templates/hash_map.h"

class Node;
class AudioStreamPlayback;
class AudioStreamPlayerInternal;
class AudioStreamGroup;

// Owns the Dummy mixer clock while a scene is held for offline inspection.
// Player updates still use their normal runtime notifications.
class WAudioStepper : public RefCounted {
	GDCLASS(WAudioStepper, RefCounted);
	static WAudioStepper *active;
	static SafeFlag clock_owned;
	static SafeFlag event_thread_error;
	static SafeFlag random_controlled;
	ObjectID root_id;
	Vector<ObjectID> players;
	double fractional_frames = 0.0;
	uint64_t advanced_frames = 0;
	bool stepping = false;
	bool releasing = false;
	double history_origin = 0;
	WAudioPlaybackHistory history;
	struct RandomInput {
		String stream;
		double unit = 0;
		Vector<uint8_t> used;
		bool consumed = false;
	};
	Vector<RandomInput> random_inputs;
	HashMap<ObjectID, Vector<uint8_t>> random_original_used;
	void random_error(const String &p_error);
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
	Error begin_random_frame();
	Error queue_random_input(const String &p_stream, double p_unit, const Vector<uint8_t> &p_used);
	Error end_random_frame();
	void end_random_inputs();
	// Called under the group's selection mutex. False retains normal randomness.
	static bool random_input(AudioStreamGroup *p_group, double &r_unit);
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
