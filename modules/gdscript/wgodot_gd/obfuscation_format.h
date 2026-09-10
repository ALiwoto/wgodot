// wgodot-changes::file

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptFormat {

inline constexpr const char *BUILTIN_ALIAS_MAP_PATH = "res://.godot/wgbca.a";
inline constexpr const char *INTERFACE_ALIAS_MAP_PATH = "res://.godot/wgima.a";
inline constexpr const char *STRING_MAP_PATH = "res://.godot/wss.a";

enum class AliasRecordKind : uint8_t {
	NATIVE_TYPE = 1,
	BUILTIN_FUNCTION = 2,
	INSTANCE_METHOD = 3,
	STATIC_METHOD = 4,
	INSTANCE_PROPERTY = 5,
	STATIC_PROPERTY = 6,
};

inline uint64_t interface_slot(uint32_t p_interface, uint32_t p_method) {
	return (static_cast<uint64_t>(p_interface) << 32) | p_method;
}

// Read directly from the decoded buffer, without allocating a byte vector per name.
inline bool read_string(const Vector<uint8_t> &p_data, int &r_offset, String &r_value) {
	const int start = r_offset;
	while (r_offset < p_data.size()) {
		if (p_data[r_offset++] == 0) {
			r_value = String::utf8(reinterpret_cast<const char *>(p_data.ptr() + start), r_offset - start - 1);
			return true;
		}
	}
	return false;
}

} // namespace WGodotGDScriptFormat
