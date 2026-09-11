// wgodot-changes::file
/**************************************************************************/
/*  interface_helpers.h                                                   */
/**************************************************************************/

#pragma once

#include "../gdscript_parser.h"

#include "core/string/string_name.h"

namespace WGodotGDScriptInterfaceHelpers {

String get_interface_id(const GDScriptParser::ClassNode *p_interface);

bool is_contract_member(const GDScriptParser::ClassNode::Member &p_member);
bool is_implemented_member(const GDScriptParser::ClassNode::Member &p_member);
bool member_has_no_mangle(const GDScriptParser::ClassNode::Member &p_member);
bool same_type(const GDScriptParser::DataType &p_first, const GDScriptParser::DataType &p_second);

bool class_implements_interface_type(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_interface);

} // namespace WGodotGDScriptInterfaceHelpers
