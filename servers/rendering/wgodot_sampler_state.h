// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

// An explicitly authored sampler, independent of viewport filtering defaults.
// Ordinary filter/repeat hints keep using the viewport's existing samplers.
struct WGodotSamplerState {
	enum Address {
		REPEAT,
		MIRROR,
		CLAMP,
	};

	bool enabled = false;
	Address address_u = CLAMP;
	Address address_v = CLAMP;
	Address address_w = CLAMP;
	uint32_t anisotropy = 1;

	uint32_t key(uint32_t p_filter = 0) const {
		return enabled ? 1u | (uint32_t(address_u) << 1) | (uint32_t(address_v) << 3) |
				(uint32_t(address_w) << 5) | (anisotropy << 7) | (p_filter << 12) : 0;
	}

	String name(uint32_t p_filter) const {
		return "wg_sampler_" + itos(key(p_filter));
	}

	bool operator==(const WGodotSamplerState &p_other) const {
		return key() == p_other.key();
	}
};
