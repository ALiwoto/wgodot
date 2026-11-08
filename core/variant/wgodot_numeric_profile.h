// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/os/os.h"
#include "core/os/thread.h"

// Opt-in parser diagnostics. Time complete numeric constructors, and sample
// one number in 1024 to separate lexical scanning from numeric conversion.
// No per-number formatting, allocations, locks, or clocks when disabled.
class WGodotNumericProfile {
	WGodotNumericProfile *previous = nullptr;
	uint64_t started = 0;
	uint64_t numbers = 0;
	uint64_t samples = 0;
	uint64_t lexical_usec = 0;
	uint64_t conversion_usec = 0;
	int line = 0;

public:
	inline static thread_local WGodotNumericProfile *active = nullptr;

	explicit WGodotNumericProfile(int p_line) {
		static const bool enabled = OS::get_singleton()->has_environment("WGODOT_PROFILE_NUMBERS");
		if (!enabled) {
			return;
		}
		previous = active;
		active = this;
		line = p_line;
		started = OS::get_singleton()->get_ticks_usec();
	}

	uint64_t begin_number() {
		++numbers;
		return (numbers & 1023) == 1 ? OS::get_singleton()->get_ticks_usec() : 0;
	}

	void finish_number(uint64_t p_started, uint64_t p_conversion) {
		++samples;
		lexical_usec += p_conversion - p_started;
		conversion_usec += OS::get_singleton()->get_ticks_usec() - p_conversion;
	}

	~WGodotNumericProfile() {
		if (!started) {
			return;
		}
		const uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - started;
		active = previous;
		if (numbers >= 4096) {
			OS::get_singleton()->print("NUMERIC_PROFILE\t%llu\t%d\t%llu\t%llu\t%llu\t%llu\t%llu\n",
					(unsigned long long)Thread::get_caller_id(), line,
					(unsigned long long)numbers, (unsigned long long)elapsed,
					(unsigned long long)samples, (unsigned long long)lexical_usec,
					(unsigned long long)conversion_usec);
		}
	}
};
#endif
