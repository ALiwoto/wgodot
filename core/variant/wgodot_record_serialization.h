// wgodot-changes::file
#pragma once

#include "core/variant/variant_parser.h"
#include "core/variant/wgodot_record.h"

namespace WGodotRecordSerialization {
Dictionary fields(const WRecord &p_value);
Error restore(const StringName &p_name, const Dictionary &p_fields, Variant &r_value);
Error parse(const String &p_constructor, VariantParser::Stream *p_stream, int &r_line, String &r_error, Variant &r_value, VariantParser::ResourceParser *p_resources);
Error write(const WRecord &p_value, VariantWriter::StoreStringFunc p_store, void *p_store_data, VariantWriter::EncodeResourceFunc p_encode, void *p_encode_data, bool p_pretty, int p_depth, bool p_compat);
} // namespace WGodotRecordSerialization
