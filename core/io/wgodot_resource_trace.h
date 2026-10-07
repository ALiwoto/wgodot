// wgodot-changes::file

#pragma once

#include "core/string/ustring.h"

// Opt-in diagnosis for synchronous editor stalls. Output goes to the engine log,
// so an unfinished begin/phase remains observable without the editor main loop.
class WGodotResourceTrace {
	uint64_t started = 0;
	uint64_t id = 0;

public:
	WGodotResourceTrace(const String &p_operation, const String &p_path = String());
	~WGodotResourceTrace();
	void phase(const String &p_phase) const;
};
