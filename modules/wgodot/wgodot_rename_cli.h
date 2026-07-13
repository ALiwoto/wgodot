// wgodot-changes::file
/**************************************************************************/
/*  wgodot_rename_cli.h                                                   */
/**************************************************************************/

#pragma once

#include "core/templates/vector.h"
#include "core/string/ustring.h"

namespace WGodotRenameCLI {

int run_source_info(const Vector<String> &p_arguments);
int run_rename(const Vector<String> &p_arguments);

} // namespace WGodotRenameCLI
