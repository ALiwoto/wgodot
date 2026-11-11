// wgodot-changes::file
#pragma once

#include "modules/gdscript/gdscript_parser.h"

// Resource field numbers are dense and private to the generated class schema.
// Schema fingerprints prevent mixing an old pack with newly generated code.
struct WGodotResourceSchema {
	Vector<const GDScriptParser::VariableNode *> fields;
	uint64_t id = 0;
	uint64_t fingerprint = 0;
	explicit WGodotResourceSchema(const GDScriptParser::ClassNode *p_class);
	uint32_t field_id(const StringName &p_name) const;
};
