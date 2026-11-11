// wgodot-changes::file
#pragma once
#include "modules/gdscript/gdscript_parser.h"
#include "modules/wgodot/codecs/resource_protobuf.h"

#include <functional>

namespace WGodotResourceEncode {
// Variant exists only at the editor's decoded-source boundary. The schema
// selects a concrete protobuf encoding before any bytes are written.
Error field(WGodotResourceProtobuf::Writer &r_writer, uint32_t p_id, const Variant &p_value,
		const GDScriptParser::DataType &p_type, WGodotResourceProtobuf::Context &p_context,
		const std::function<int64_t(const String &)> &p_path);
} //namespace WGodotResourceEncode
