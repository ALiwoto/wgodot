// wgodot-changes::file

#pragma once

#include "modules/gdscript/gdscript_parser.h"

namespace WGodotGDScriptExportTransform {

struct ExportScope {
	const GDScriptParser::ClassNode *current_class = nullptr;
	bool no_mangle = false;
	bool no_string_mangle = false;
};

// Shared traversal only. Feature decisions belong to the visitor using it.
class ExportASTVisitor {
	HashSet<const GDScriptParser::Node *> visited;
	void walk_node(const GDScriptParser::Node *p_node, ExportScope p_scope);

protected:
	void walk_child(const GDScriptParser::Node *p_node, const ExportScope &p_scope) { walk_node(p_node, p_scope); }
	virtual bool enter(const GDScriptParser::Node *p_node, const ExportScope &p_scope) = 0;

public:
	void walk(const GDScriptParser::Node *p_node);
	virtual ~ExportASTVisitor() = default;
};

} // namespace WGodotGDScriptExportTransform
