// wgodot-changes::file
#pragma once

#include "core/object/object.h"

namespace WGodotMethodEvent {
bool validate_parameterless(const StringName &p_target, const StringName &p_method, String &r_error);
bool matches(Object *p_object, const StringName &p_target);
} // namespace WGodotMethodEvent
