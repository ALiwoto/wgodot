// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Owned observation records: no player/playback references prolong lifetimes.
struct WAudioPlaybackEvent {
	String kind;
	String player;
	String playback_id;
	String stream;
	String bus;
	double seconds = 0;
	double offset = 0;
	float volume_db = 0;
	float pitch = 1;
};

struct WAudioPlaybackHistory {
	bool enabled = false;
	double seconds = 0;
	Vector<WAudioPlaybackEvent> events;
	Vector<String> errors;
};
#endif
