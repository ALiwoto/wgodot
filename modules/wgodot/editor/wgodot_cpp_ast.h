// wgodot-changes::file
#pragma once

#include "modules/gdscript/gdscript_parser.h"

class WGodotCppAstVisitor {
protected:
	virtual bool visit(const GDScriptParser::Node *p_node) = 0;
	virtual bool descend(const GDScriptParser::Node *p_node) { return true; }

public:
	// Only follows owned child nodes, never parent links or resolved symbol references.
	bool walk(const GDScriptParser::Node *p_node);
	virtual ~WGodotCppAstVisitor() = default;
};
