// wgodot-changes::file
/**************************************************************************/
/*  wgodot_source_info.h                                                  */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotSourceInfo {

Dictionary resolve(const Dictionary &p_options);
Dictionary rename_preflight(const Dictionary &p_options);
Dictionary rename_complete();

} // namespace WGodotSourceInfo
