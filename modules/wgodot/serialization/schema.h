// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "wire.h"

#include "modules/gdscript/gdscript_parser.h"

namespace WGodotSerialization {
enum class Codec { JSON, BINARY, PROTOBUF };

struct Field {
	const GDScriptParser::VariableNode *node = nullptr;
	GDScriptParser::DataType type;
	int id = -1;
	Kind kind = Kind::INVALID;
	Kind key = Kind::INVALID;
	Kind value = Kind::INVALID;
	bool packed = true;
	String oneof;
	String json;
};

struct Schema {
	bool enabled = false;
	bool binary = false;
	bool protobuf = false;
	bool big_endian = false;
	bool strict = true;
	Vector<Field> fields;
	String error;
	const GDScriptParser::Node *error_node = nullptr;
};

Schema schema(const GDScriptParser::ClassNode *p_class, Codec p_codec);
} // namespace WGodotSerialization
#endif
