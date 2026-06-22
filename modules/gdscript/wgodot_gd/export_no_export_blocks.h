// wgodot-changes::file
/**************************************************************************/
/*  export_no_export_blocks.h                                             */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptExportTransform {

String strip_no_export_blocks(const String &p_source, const String &p_path, bool *r_changed = nullptr);

} // namespace WGodotGDScriptExportTransform
