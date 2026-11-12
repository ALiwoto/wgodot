// wgodot-changes::file
#pragma once

#include "core/variant/variant_parser.h"

namespace WGodotClassReferenceSerialization {
Error parse(VariantParser::Stream *p_stream, Variant &r_value, int &r_line, String &r_error, bool p_syntax_only);
Error parse_type(VariantParser::Stream *p_stream, StringName &r_constraint, int &r_line, String &r_error, bool p_syntax_only);
String type_text(const StringName &p_constraint);
} // namespace WGodotClassReferenceSerialization
