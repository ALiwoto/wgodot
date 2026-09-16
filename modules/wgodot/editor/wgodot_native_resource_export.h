// wgodot-changes::file
#pragma once

#include "core/variant/dictionary.h"

// Rewrites script attachments in Godot's text serialization. All other values
// retain their original spelling and are loaded by Godot's normal loaders.
Error wgodot_export_native_resource(const String &p_source, const Dictionary &p_classes, String &r_output, String &r_error);
