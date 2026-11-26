// wgodot-changes::file
#pragma once
#include "core/string/ustring.h"

// Cross-process exclusion for the generated module and its staged publications.
// OS ownership is released on process exit, including interrupted builds.
class WGodotNativeBuildLock {
	intptr_t handle = -1;

public:
	Error acquire(const String &p_directory);
	~WGodotNativeBuildLock();
};
