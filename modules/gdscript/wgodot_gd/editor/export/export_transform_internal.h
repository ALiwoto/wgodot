// wgodot-changes::file
/**************************************************************************/
/*  export_transform_internal.h                                           */
/**************************************************************************/

#pragma once

#include "export_context.h"
#include "export_timing.h"
#include "export_transform.h"
#include "source_rewrite.h"

#include "core/templates/vector.h"
#include "core/variant/variant.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"

namespace WGodotGDScriptExportTransform {

String get_parser_errors_with_source_text(const GDScriptParser &p_parser, const String &p_source);

void add_extends_path_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class);

String get_export_string_literal_replacement(RewriteContext &r_context, Variant::Type p_type, const String &p_value);
void add_string_literal_replacement(RewriteContext &r_context, const GDScriptParser::LiteralNode *p_literal);
bool add_string_concat_replacement(RewriteContext &r_context, const GDScriptParser::BinaryOpNode *p_binary);
bool should_strip_export_annotation(const GDScriptParser::AnnotationNode *p_annotation);
void add_annotation_strip_replacement(RewriteContext &r_context, const GDScriptParser::AnnotationNode *p_annotation);
void collect_comment_replacements(RewriteContext &r_context, const GDScriptParser &p_parser);
void collect_empty_line_replacements(RewriteContext &r_context);
bool overlaps_existing_replacement(RewriteContext &r_context, int p_start, int p_end);

} // namespace WGodotGDScriptExportTransform
