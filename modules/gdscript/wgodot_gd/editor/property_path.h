// wgodot-changes::file

#pragma once

#include "core/string/node_path.h"

#include "modules/gdscript/gdscript_parser.h"

struct WGodotGDScriptPropertyPath {
	struct Segment {
		StringName name;
		GDScriptParser::DataType base_type;
		GDScriptParser::DataType datatype;
		GDScriptParser::ClassNode *owner = nullptr;
		GDScriptParser::ClassNode::Member member;
	};

	NodePath path;
	Vector<Segment> segments;
};
