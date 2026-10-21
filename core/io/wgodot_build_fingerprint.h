// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

// Identity of the running engine, including local changes and build options.
// Calculated once per process; shared by caches whose interpretation is executable code.
namespace WGodotBuildFingerprint {
const String &get();
}
