// wgodot-changes::file
#pragma once

// Foreground tool operations can use the full pool while keeping ordinary
// background/game loading at its normal priority. Affects requests on this thread.
class WGodotResourceLoadPriority {
	static inline thread_local bool foreground = false;
	bool previous;

public:
	explicit WGodotResourceLoadPriority(bool p_foreground) : previous(foreground) { foreground = p_foreground; }
	~WGodotResourceLoadPriority() { foreground = previous; }
	WGodotResourceLoadPriority(const WGodotResourceLoadPriority &) = delete;
	WGodotResourceLoadPriority &operator=(const WGodotResourceLoadPriority &) = delete;
	static bool is_foreground() { return foreground; }
};
