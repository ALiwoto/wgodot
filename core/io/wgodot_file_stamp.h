// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

// Development cache identity, not an integrity/security checksum. Uses the
// filesystem's full timestamp precision; never reads file contents.
namespace WGodotFileStamp {
String get(const String &p_path);
}
