// wgodot-changes::file
#pragma once

#include "wgodot_native_dictionary_fwd.h"

#include "core/string/ustring.h"

namespace WGodotNative {

String format_named(const String &p_source, const WDictionary<String, String> &p_values, const String &p_placeholder = "{_}");

} // namespace WGodotNative
