// wgodot-changes::file
#pragma once

#include "core/object/property_info.h"
#include "core/variant/variant.h"
#include <functional>

namespace WGodotResourceRewrite {
using PathMapper = std::function<String(const String &)>;
Variant value(const Variant &p_value, const PropertyInfo &p_property, const PathMapper &p_path);
String text(const String &p_text, const String &p_source, const PathMapper &p_path);
} // namespace WGodotResourceRewrite
