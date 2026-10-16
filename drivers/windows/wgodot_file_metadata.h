// wgodot-changes::file
#pragma once

#include <cstdint>
#include <windows.h>

// FileAccess timestamps use seconds since the Unix epoch. Both the directory
// metadata and handle APIs return the same Windows FILETIME representation.
static uint64_t wgodot_file_time_seconds(const FILETIME &p_write, const FILETIME &p_create) {
	const FILETIME &time = (p_write.dwHighDateTime || p_write.dwLowDateTime) ? p_write : p_create;
	const uint64_t ticks = (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
	const uint64_t epoch = 116444736000000000ULL;
	return ticks >= epoch ? (ticks - epoch) / 10000000ULL : 0;
}
