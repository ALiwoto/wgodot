// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/object_id.h"
#include "core/templates/hash_set.h"
#include "scene/resources/audio/audio_stream.h"

class Node;

// Owned diagnostic records. Capture on the main thread while holding the
// audio driver lock; no playback pointers escape to a later inspection.
class WAudioPlaybackProbe {
public:
	struct Voice {
		String path;
		String stream_path;
		String stream_class;
		double position = 0;
		double duration = 0;
		float volume = 1;
		float pitch = 1;
		double delay = 0;
		int loops = 0;
		int selection = -1;
		bool playing = false;
		bool pending = false;
		bool finishing = false;
		bool leaf = false;
		bool children_supported = false;
		bool looping = false;
		bool metadata_only = false;
	};
	struct Snapshot {
		Vector<Voice> voices;
		Vector<String> errors;
	};

	static Snapshot player(Node *p_player);

private:
	static void append(Snapshot &r_snapshot, const Ref<AudioStream> &p_stream,
			const Ref<AudioStreamPlayback> &p_playback, const String &p_path,
			float p_volume, float p_pitch, bool p_pending, bool p_finishing,
			HashSet<ObjectID> &r_ancestors);
};
#endif
