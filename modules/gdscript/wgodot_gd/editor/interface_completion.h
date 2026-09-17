// wgodot-changes::file

#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/editor_language.h"

namespace WGodotGDScriptEditor {
void find_builtin_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result);
void find_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result);
void find_native_interface_members(const StringName &p_type, bool p_static, bool p_only_functions, bool p_types_only, bool p_add_braces, HashMap<String, EditorLanguage::CompletionOption> &r_result);
} // namespace WGodotGDScriptEditor
#endif
