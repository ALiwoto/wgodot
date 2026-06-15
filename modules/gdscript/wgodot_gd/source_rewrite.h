// wgodot-changes::file
/**************************************************************************/
/*  source_rewrite.h                                                      */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "../gdscript_parser.h"

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptExportTransform {

struct Replacement {
	int start = 0;
	int end = 0;
	String text;
};

struct RewriteContext {
	String source;
	TransformOptions options;
	Vector<int> line_offsets;
	Vector<Replacement> replacements;
	HashSet<StringName> reserved_obfuscated_names;
	HashSet<const GDScriptParser::ConstantNode *> no_mangle_constants;
	HashMap<const GDScriptParser::Node *, String> obfuscated_local_names;
	HashMap<const GDScriptParser::FunctionNode *, String> obfuscated_function_names;
	int obfuscated_local_counter = 0;
};

void build_line_offsets(RewriteContext &r_context);
int get_line_start_offset(const RewriteContext &p_context, int p_line);
int get_line_end_offset(const RewriteContext &p_context, int p_line);
int get_offset(const RewriteContext &p_context, int p_line, int p_column);
void add_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const String &p_text);
String apply_replacements(RewriteContext &r_context);

} // namespace WGodotGDScriptExportTransform
