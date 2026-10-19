// wgodot-changes::file
#pragma once

// Enable per-resource decoding reports with WGODOT_ANIMATION_CODEC_TRACE=1.
// No counters, timers or logging are compiled into non-debug game builds.
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
#define W_ANIMATION_CODEC_DIAGNOSTICS

#include "core/error/error_list.h"
#include "core/string/ustring.h"

class WAnimationCodecProfile {
public:
	enum Phase { CONSTANTS, CLOCKS, TRACKS, PHASE_COUNT };

private:
	bool enabled = false;
	uint64_t started = 0;
	uint64_t phase_started = 0;
	uint64_t phase_usec[PHASE_COUNT] = {};
	Phase current_phase = CONSTANTS;

public:
	uint64_t literals = 0;
	uint64_t references = 0;
	uint64_t keys = 0;
	uint64_t key_bytes = 0;
	uint64_t clock_values = 0;
	uint64_t tracks = 0;
	uint64_t constants = 0;
	uint64_t constant_table_bytes = 0;
	uint64_t attach_usec = 0;

	WAnimationCodecProfile();
	bool active() const { return enabled; }
	void phase(Phase p_phase);
	void finish(const String &p_path, int p_version, Error p_status);
};

#define W_ANIMATION_CODEC_COUNT(m_profile, m_field, m_count) \
	if (unlikely((m_profile).active())) { \
		(m_profile).m_field += (m_count); \
	}
#else
#define W_ANIMATION_CODEC_COUNT(m_profile, m_field, m_count) ((void)0)
#endif
