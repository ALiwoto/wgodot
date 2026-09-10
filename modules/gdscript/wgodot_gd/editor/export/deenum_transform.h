// wgodot-changes::file
/**************************************************************************/
/*  deenum_transform.h                                                    */
/**************************************************************************/

#pragma once

#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

const GDScriptParser::EnumNode *find_deenum_enum_for_datatype(const RewriteContext &p_context, const GDScriptParser::DataType &p_datatype);
bool should_deenum_enum(const RewriteContext &p_context, const GDScriptParser::EnumNode *p_enum);
bool add_enum_type_replacement(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type);
bool add_enum_identifier_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);
bool add_enum_attribute_reference_replacement(RewriteContext &r_context, const GDScriptParser::SubscriptNode *p_subscript);
bool add_enum_declaration_replacement(RewriteContext &r_context, const GDScriptParser::EnumNode *p_enum, bool p_leave_pass);

} // namespace WGodotGDScriptExportTransform
