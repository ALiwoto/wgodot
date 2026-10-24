// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli.h                                                          */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/variant/dictionary.h"

namespace WGodotCLI {

constexpr int PROTOCOL_VERSION = 1;
constexpr uint64_t PROJECT_OPERATION_TIMEOUT_MSEC = 300000;

bool waits_for_editor(const String &p_command);
uint64_t project_operation_timeout(const Dictionary &p_request);

bool extract_arguments(List<String> &r_arguments, String &r_project_path);
bool is_command_requested();
bool execute_if_requested(int &r_exit_code);

int request_editor_command(const Dictionary &p_request, Dictionary &r_response);
String make_absolute_output_path(const String &p_path);

String get_current_project_root();
String get_project_key(const String &p_project_root);
String get_agents_root_directory();
String get_project_agents_directory(const String &p_project_key);

} // namespace WGodotCLI
