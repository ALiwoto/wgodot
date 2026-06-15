// wgodot-changes::file
/**************************************************************************/
/*  deconst_transform.h                                                   */
/**************************************************************************/

#pragma once

#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

bool should_deconst_constant(const RewriteContext &p_context, const GDScriptParser::ConstantNode *p_constant);
bool is_declared_constant_identifier(const RewriteContext &p_context, const GDScriptParser::IdentifierNode *p_identifier);
void add_constant_reference_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const GDScriptParser::ConstantNode *p_constant);
void add_constant_declaration_replacement(RewriteContext &r_context, const GDScriptParser::ConstantNode *p_constant, bool p_leave_pass);

} // namespace WGodotGDScriptExportTransform
