// wgodot-changes::file
/**************************************************************************/
/*  string_obfuscation.h                                                  */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace WGodotGDScriptExportTransform {
class ExportContext;
}

namespace WGodotGDScriptStringObfuscation {

String get_string_map_path();
Vector<uint8_t> serialize_string_map(const WGodotGDScriptExportTransform::ExportContext &p_context);
void clear_runtime_cache();

String make_uncached_obfuscated_string_literal_source(WGodotGDScriptExportTransform::ExportContext &r_context, Variant::Type p_type, const String &p_value);
String make_single_character_string_literal_source(Variant::Type p_type, const String &p_value);
Variant decode_obfuscated_literal(const Variant &p_literal);

} // namespace WGodotGDScriptStringObfuscation
