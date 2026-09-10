// wgodot-changes::file

#pragma once

#include "core/variant/variant.h"

namespace WGodotGDScriptStringObfuscation {
String get_string_map_path();
void clear_runtime_cache();
Variant decode_obfuscated_literal(const Variant &p_literal);
} //namespace WGodotGDScriptStringObfuscation
