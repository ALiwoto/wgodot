// wgodot-changes::file
/**************************************************************************/
/*  deadcode_injection.h                                                  */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "core/string/ustring.h"

namespace WGodotGDScriptDeadCodeInjection {

String inject_in_class_dead_code(const String &p_source, const String &p_path, const WGodotGDScriptExportTransform::TransformOptions &p_options, bool *r_changed = nullptr);

} // namespace WGodotGDScriptDeadCodeInjection
