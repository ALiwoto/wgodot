// wgodot-changes::file

#ifndef WGODOT_STARTUP_PROFILE_H
#define WGODOT_STARTUP_PROFILE_H

#include "core/string/ustring.h"

namespace WGodotStartupProfile {

void enable();

// Timings include nested work; self time excludes instrumented children on the
// same thread. Only scopes lasting at least 1 ms are printed.
class Scope {
	static thread_local Scope *current;
	Scope *parent = nullptr;
	const char *operation = nullptr;
	String detail;
	uint64_t started_usec = 0;
	uint64_t children_usec = 0;
	bool stop_after = false;

	void begin(const char *p_operation);

public:
	Scope(const char *p_operation, const String &p_detail = String(), bool p_stop_after = false);
	~Scope();
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;

	void next(const char *p_operation);
	void finish();
};

} // namespace WGodotStartupProfile

#endif // WGODOT_STARTUP_PROFILE_H
