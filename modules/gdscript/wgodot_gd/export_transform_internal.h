// wgodot-changes::file
/**************************************************************************/
/*  export_transform_internal.h                                           */
/**************************************************************************/

#pragma once

#include "export_context.h"
#include "export_timing.h"
#include "export_transform.h"
#include "source_rewrite.h"

#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace WGodotGDScriptExportTransform {

struct AnalyzedSource {
	GDScriptParser local_parser;
	Ref<GDScriptParserRef> cached_parser_ref;
	GDScriptParser *parser = nullptr;

	bool load(const String &p_source, const String &p_path, String *r_error_details = nullptr);

private:
	bool load_from_cache(const String &p_source, const String &p_path);
	bool load_local(const String &p_source, const String &p_path, String *r_error_details);
};

struct ScriptSource {
	String path;
	String source;
	AnalyzedSource *analyzed_source = nullptr;
};

struct GlobalClassRenameRequest {
	StringName name;
	String path;
};

String get_parser_errors_with_source_text(const GDScriptParser &p_parser, const String &p_source);
bool parse_only(const String &p_source, const String &p_path, String *r_error_details = nullptr);

void collect_export_replacements(RewriteContext &r_context, const GDScriptParser &p_parser, ExportReplacementTiming *r_timing = nullptr);
void collect_string_obfuscation_resources(const String &p_source, const String &p_path, ExportContext *p_context);
void collect_string_obfuscation_resources_from_tree(const String &p_source, const String &p_path, const GDScriptParser::ClassNode *p_tree, ExportContext *p_context);

void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node);
bool has_obfuscate_path_annotation(const GDScriptParser::ClassNode *p_class);
void reserve_script_global_class_name_from_source(ExportContext *p_context, const String &p_source, const String &p_path);
void collect_global_class_rename_request(ExportContext *p_context, const GDScriptParser::ClassNode *p_class, const String &p_path, Vector<GlobalClassRenameRequest> &r_requests);

void add_builtin_function_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call);
void add_builtin_method_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call);
void add_builtin_property_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier);
void add_builtin_class_alias_name_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_builtin_class_alias_type_replacement(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type);

String get_export_string_literal_replacement(RewriteContext &r_context, Variant::Type p_type, const String &p_value);
void add_string_literal_replacement(RewriteContext &r_context, const GDScriptParser::LiteralNode *p_literal);
bool add_string_concat_replacement(RewriteContext &r_context, const GDScriptParser::BinaryOpNode *p_binary);
bool should_strip_export_annotation(const GDScriptParser::AnnotationNode *p_annotation);
void add_annotation_strip_replacement(RewriteContext &r_context, const GDScriptParser::AnnotationNode *p_annotation);
void collect_annotation_replacements(RewriteContext &r_context, const GDScriptParser::Node *p_node, bool p_no_mangle_scope);
void collect_comment_replacements(RewriteContext &r_context, const GDScriptParser &p_parser);
void collect_empty_line_replacements(RewriteContext &r_context);
bool overlaps_existing_replacement(RewriteContext &r_context, int p_start, int p_end);
bool is_no_mangle_property_scope(const GDScriptParser::VariableNode *p_variable);

} // namespace WGodotGDScriptExportTransform
