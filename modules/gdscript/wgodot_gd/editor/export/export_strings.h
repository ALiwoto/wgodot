// wgodot-changes::file

#pragma once

#include "core/variant/variant.h"

namespace WGodotGDScriptExportTransform {
class ExportContext;
}

namespace WGodotGDScriptStringObfuscation {
String make_uncached_obfuscated_string_literal_source(WGodotGDScriptExportTransform::ExportContext &r_context, Variant::Type p_type, const String &p_value);
String make_single_character_string_literal_source(Variant::Type p_type, const String &p_value);
} //namespace WGodotGDScriptStringObfuscation
