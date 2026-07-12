// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli.h                                                          */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/list.h"

namespace WGodotCLI {

constexpr int PROTOCOL_VERSION = 1;

bool extract_arguments(List<String> &r_arguments, String &r_project_path);
bool execute_if_requested(int &r_exit_code);

String get_current_project_root();
String get_project_key(const String &p_project_root);
String get_agents_root_directory();
String get_project_agents_directory(const String &p_project_key);

} // namespace WGodotCLI
