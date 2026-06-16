// wgodot-changes::file
/**************************************************************************/
/*  interface_helpers.h                                                   */
/**************************************************************************/

#pragma once

#include "../gdscript_parser.h"

#include "core/string/string_name.h"

namespace WGodotGDScriptInterfaceHelpers {

bool class_implements_interface_type(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_interface);

} // namespace WGodotGDScriptInterfaceHelpers
