// wgodot-changes::file
/**************************************************************************/
/*  interface_helpers.h                                                   */
/**************************************************************************/

#pragma once

#include "../gdscript_parser.h"

#include "core/object/wgodot_interface_registry.h"
#include "core/string/string_name.h"

namespace WGodotGDScriptInterfaceHelpers {

String get_interface_id(const GDScriptParser::ClassNode *p_interface);

bool same_type(const GDScriptParser::DataType &p_first, const GDScriptParser::DataType &p_second);

const WGodotNativeInterfaces::Descriptor *native_interface_for_member(const GDScriptParser::DataType &p_type, const StringName &p_member);
bool class_implements_native_interface(const GDScriptParser::ClassNode *p_class, const StringName &p_interface);

bool class_implements_interface_type(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_interface);

} // namespace WGodotGDScriptInterfaceHelpers
