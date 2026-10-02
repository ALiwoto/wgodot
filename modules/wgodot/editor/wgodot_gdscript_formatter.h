// wgodot-changes::file

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptFormatter {

struct Options {
	int line_length = 88;
	bool explicit_self = true;
};

// Parses both versions and verifies syntax and statement structure before returning output.
bool format(const String &p_source, const String &p_path, const Options &p_options, String &r_output, String &r_error);

} // namespace WGodotGDScriptFormatter
