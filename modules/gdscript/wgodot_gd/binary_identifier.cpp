// wgodot-changes::file

#include "binary_identifier.h"

namespace WGodotGDScript {

String unwrap_binary_identifier_escape(const String &p_name) {
	if (p_name.begins_with("${{") && p_name.ends_with("}}") && p_name.length() >= 5) {
		return p_name.substr(3, p_name.length() - 5);
	}

	return p_name;
}

} // namespace WGodotGDScript
