// wgodot-changes::file
/**************************************************************************/
/*  wgodot_logs_cli.h                                                     */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotLogsCLI {

int run(const String &p_command, const Vector<String> &p_arguments);

} // namespace WGodotLogsCLI
