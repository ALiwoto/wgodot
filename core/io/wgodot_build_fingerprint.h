// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

// Local build identity from executable metadata, shared by development caches.
namespace WGodotBuildFingerprint {
const String &get();
}
