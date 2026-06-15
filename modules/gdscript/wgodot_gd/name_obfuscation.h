// wgodot-changes::file
/**************************************************************************/
/*  name_obfuscation.h                                                    */
/**************************************************************************/

#pragma once

#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

void collect_suite_local_name_obfuscation(RewriteContext &r_context, const GDScriptParser::SuiteNode *p_suite);
void add_local_name_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier);

} // namespace WGodotGDScriptExportTransform
