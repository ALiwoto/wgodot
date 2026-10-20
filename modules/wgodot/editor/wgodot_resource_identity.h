// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

namespace WGodotResourceIdentity {
// Input is a normalized project path. IDs do not depend on discovery order,
// worker count or other assets. 52 bits remain exact in the JSON build manifest.
inline int64_t from_path(const String &p_path) {
	return p_path.sha256_text().substr(0, 13).hex_to_int();
}
} // namespace WGodotResourceIdentity
