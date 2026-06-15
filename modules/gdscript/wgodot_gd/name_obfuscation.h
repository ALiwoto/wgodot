// wgodot-changes::file
/**************************************************************************/
/*  name_obfuscation.h                                                    */
/**************************************************************************/

#pragma once

#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

void collect_suite_local_name_obfuscation(RewriteContext &r_context, const GDScriptParser::SuiteNode *p_suite);
void add_local_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void collect_private_member_name_obfuscation(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope);
void add_private_member_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_private_function_pointer_replacement(RewriteContext &r_context, const GDScriptParser::ClassNode *p_class, const GDScriptParser::IdentifierNode *p_identifier);

} // namespace WGodotGDScriptExportTransform
