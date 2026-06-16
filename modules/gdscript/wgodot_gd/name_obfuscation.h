// wgodot-changes::file
/**************************************************************************/
/*  name_obfuscation.h                                                    */
/**************************************************************************/

#pragma once

#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

void collect_suite_local_name_obfuscation(RewriteContext &r_context, const GDScriptParser::SuiteNode *p_suite);
void add_local_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void collect_member_name_obfuscation(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope);
void add_class_declaration_name_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class);
void add_builtin_class_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_global_class_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_attribute_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier);
void add_call_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call);
void add_function_pointer_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, const GDScriptParser::IdentifierNode *p_identifier);

} // namespace WGodotGDScriptExportTransform
