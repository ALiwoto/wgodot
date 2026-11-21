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
	struct OutputClock {
		uint64_t mixed_frames = 0;
		int queued_frames = 0;
		uint32_t block_frames = 0;
		int lookahead_frames = 0;
	};
	struct Voice {
		String path;
		String stream_path;
		String stream_class;
		double position = 0;
		double buffered_frames = -1;
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
		int input_channels = 0;
		int sample_rate = 0;
		int group_mode = -1;
		int group_streams = 0;
		bool group_loop = false;
		int pan_channels = 0;
		bool pan_spatial = false;
		Vector<double> pan_matrix;
	};
	struct Route {
		String bus;
		Vector<double> gains;
	};
	struct Mixer {
		String path;
		int state = -1;
		float pitch = 1;
		float highshelf_gain = 0;
		float cutoff_hz = 0;
		Vector<Route> routes;
	};
	struct Snapshot {
		Vector<Voice> voices;
		Vector<Mixer> mixers;
		Vector<String> errors;
	};

	static Snapshot player(Node *p_player);
	static OutputClock output_clock();

private:
	static void append(Snapshot &r_snapshot, const Ref<AudioStream> &p_stream,
			const Ref<AudioStreamPlayback> &p_playback, const String &p_path,
			float p_volume, float p_pitch, bool p_pending, bool p_finishing,
			HashSet<ObjectID> &r_ancestors);
};
#endif
