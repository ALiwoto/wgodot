// wgodot-changes::file
/**************************************************************************/
/*  wgodot_lsp_client.h                                                   */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotLSPClient {

Dictionary request_rename(const String &p_host, int p_port, const String &p_project_root, const String &p_source_path, int p_line, int p_character, const String &p_new_name);

} // namespace WGodotLSPClient
