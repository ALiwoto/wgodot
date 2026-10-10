// wgodot-changes::file
/**************************************************************************/
/*  wgodot_logs_cli.h                                                     */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace WGodotLogsCLI {

int run(const String &p_command, const Vector<String> &p_arguments);
void print_debugger_entries(const Dictionary &p_response);

} // namespace WGodotLogsCLI
