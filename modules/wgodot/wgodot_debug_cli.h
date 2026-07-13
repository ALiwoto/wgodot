// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_cli.h                                                    */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotDebugCLI {

int run_breakpoint(const Vector<String> &p_arguments);
int run_debug(const Vector<String> &p_arguments);

} // namespace WGodotDebugCLI
