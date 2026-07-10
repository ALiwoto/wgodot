// wgodot-changes::file
/**************************************************************************/
/*  source_rewrite.h                                                      */
/**************************************************************************/

#pragma once

#include "export_context.h"
#include "export_transform.h"

#include "../gdscript_parser.h"

#include "core/math/random_pcg.h"
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
	String script_path;
	TransformOptions options;
	ExportContext *export_context = nullptr;
	const GDScriptParser::ClassNode *current_class = nullptr;
	bool no_string_mangle_scope = false;
	bool timing_enabled = false;
	Vector<int> line_offsets;
	Vector<Replacement> replacements;
	HashSet<StringName> reserved_obfuscated_names;
	HashSet<const GDScriptParser::EnumNode *> visited_enum_declarations;
	HashSet<const GDScriptParser::EnumNode *> removed_enum_declarations;
	HashMap<const GDScriptParser::Node *, String> obfuscated_local_names;
	HashMap<const GDScriptParser::ClassNode *, String> obfuscated_class_names;
	HashMap<const GDScriptParser::FunctionNode *, String> obfuscated_function_names;
	HashMap<const GDScriptParser::SignalNode *, String> obfuscated_signal_names;
	HashMap<const GDScriptParser::VariableNode *, String> obfuscated_variable_names;
	uint64_t local_name_make_calls = 0;
	uint64_t local_name_make_usec = 0;
	uint64_t string_literal_replacement_calls = 0;
	uint64_t string_literal_replacement_usec = 0;
	uint64_t string_concat_replacement_calls = 0;
	uint64_t string_concat_replacement_usec = 0;
	uint64_t overlap_check_count = 0;
	uint64_t overlap_scanned_replacements = 0;
	uint64_t overlap_check_usec = 0;
	RandomPCG obfuscation_random;
};

void build_line_offsets(RewriteContext &r_context);
int get_line_start_offset(const RewriteContext &p_context, int p_line);
int get_line_end_offset(const RewriteContext &p_context, int p_line);
int get_offset(const RewriteContext &p_context, int p_line, int p_column);
void add_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const String &p_text);
String apply_replacements(RewriteContext &r_context);

} // namespace WGodotGDScriptExportTransform
