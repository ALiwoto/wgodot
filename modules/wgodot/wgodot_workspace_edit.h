// wgodot-changes::file
/**************************************************************************/
/*  wgodot_workspace_edit.h                                               */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotWorkspaceEdit {

Dictionary apply(const Dictionary &p_workspace_edit, const String &p_project_root, const String &p_expected_name, const String &p_new_name, bool p_dry_run);

} // namespace WGodotWorkspaceEdit
