// wgodot-changes::file
#pragma once

#include "core/io/file_access.h"

// Opt-in export diagnostics. Samples run on the export owner, outside worker
// groups; OS process/working-set sampling can be correlated by elapsed time.
class WGodotCppMemoryProfile {
	static thread_local WGodotCppMemoryProfile *active;
	WGodotCppMemoryProfile *previous = nullptr;
	Ref<FileAccess> samples;
	Ref<FileAccess> resources;
	uint64_t started = 0;

public:
	explicit WGodotCppMemoryProfile(const String &p_directory);
	~WGodotCppMemoryProfile();
	WGodotCppMemoryProfile(const WGodotCppMemoryProfile &) = delete;
	WGodotCppMemoryProfile &operator=(const WGodotCppMemoryProfile &) = delete;
	bool is_open() const { return samples.is_valid() && resources.is_valid(); }
	static void sample(const char *p_phase, const String &p_source = String(), int64_t p_items = 0);
	static void snapshot_resources(const char *p_phase);
};
