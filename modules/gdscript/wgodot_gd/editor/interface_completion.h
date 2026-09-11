// wgodot-changes::file

#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/editor_language.h"

namespace WGodotGDScriptEditor {
void find_builtin_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result);
void find_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result);
} // namespace WGodotGDScriptEditor
#endif
