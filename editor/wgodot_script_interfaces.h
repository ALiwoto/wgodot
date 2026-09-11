// wgodot-changes::file
#pragma once

#include "core/object/script_language.h"

namespace WGodotEditorInterfaces {
bool accepts_object(const StringName &p_interface, const Object *p_object);
bool accepts_type(const StringName &p_interface, const StringName &p_type);
}
