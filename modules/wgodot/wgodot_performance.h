// wgodot-changes::file
#pragma once

#include "core/variant/dictionary.h"

namespace WGodotPerformance {

enum FramePhase {
	PREPARE,
	PHYSICS,
	PROCESS,
	DEFERRED_CALLS,
	NAVIGATION,
	RENDER_SYNC,
	RENDER_DRAW,
	DIAGNOSTICS,
	EXTENSIONS,
	SCRIPT_SERVERS,
	AUDIO,
	DEBUGGER,
	FRAME_PACING,
	PHASE_COUNT,
};

enum FrameRegion {
	SCENE_INSTANTIATE,
	NODE_ATTACH,
	DELETE_QUEUE,
	SCRIPT_INSTANCE_FREE,
	ANIMATION_FREE,
	ANIMATION_CACHES,
	ANIMATION_EVALUATE,
	RESOURCE_LOAD,
	REGION_COUNT,
};

// Main-thread timings only, active during a perf capture. Recursion within
// one region is counted but timed once. Different regions may overlap.
class RegionScope {
	FrameRegion region;
	uint64_t started_usec = 0;
	bool entered = false;

public:
	explicit RegionScope(FrameRegion p_region);
	~RegionScope();
};

void begin_frame();
void begin_phase(FramePhase p_phase);
Dictionary execute(uint64_t p_request_id, const String &p_command, const Dictionary &p_options, bool &r_deferred);
void end_frame();
// Deliver completed worker reports without advancing or finalizing a frame.
// Called on the main thread, including while the script debugger is paused.
void poll_completed_reports();
void reset();

// Covers the whole iteration, including early returns and work after drawing.
class FrameScope {
public:
	FrameScope() { begin_frame(); }
	~FrameScope() { end_frame(); }
};

} // namespace WGodotPerformance
