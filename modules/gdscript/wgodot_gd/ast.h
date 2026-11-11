// wgodot-changes::file
#pragma once

#include "modules/gdscript/gdscript_parser.h"

class WGodotGDScriptAstVisitor {
protected:
	virtual bool visit(const GDScriptParser::Node *p_node) = 0;
	virtual bool descend(const GDScriptParser::Node *p_node) { return true; }

public:
	// Only follows owned children, never parent links or resolved symbols.
	bool walk(const GDScriptParser::Node *p_node);
	virtual ~WGodotGDScriptAstVisitor() = default;
};
