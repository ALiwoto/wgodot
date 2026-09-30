// wgodot-changes::file
#pragma once

#include "core/string/string_name.h"

class Variant;

// Native game identities have no dependency on the original project filenames.
// Entry zero is reserved for the export catalog; zero as a handle means absent.
namespace WGodotResourcePaths {
inline constexpr uint32_t CATALOG_MAGIC = 0x31524357; // WRC1.
inline constexpr const char *CATALOG_PATH = "res://0";

String to_path(int64_t p_id);
int64_t from_path(const String &p_path);
int64_t from_variant(const Variant &p_value);
bool is_opaque(const String &p_path);
String remap(const String &p_path);
StringName resource_type(const String &p_path);
bool recognizes_extension(const String &p_path, const String &p_extension);
} // namespace WGodotResourcePaths
