// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/error/error_list.h"
#include "core/string/ustring.h"

// Optional graphics debugger integration. All capture calls run on the render
// thread; the library must be loaded before creating the graphics device.
class WRenderDocCapture {
public:
	static Error initialize(const String &p_library);
	static bool is_available();
	static Error request(const String &p_directory, int p_index, int64_t p_frame, double p_seconds);
	// Query after the submitted frame, before another sample or capture teardown.
	static Error get_result();
	static void begin_frame();
	static void end_frame();
};
#endif
