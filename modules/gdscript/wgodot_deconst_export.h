// wgodot-changes::file
/**************************************************************************/
/*  wgodot_deconst_export.h                                               */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

namespace WGodotGDScriptDeconstExport {

String sanitize_source(const String &p_source, const String &p_path, bool *r_changed = nullptr);

} // namespace WGodotGDScriptDeconstExport
