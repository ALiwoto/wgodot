// wgodot-changes::file
/**************************************************************************/
/*  wgodot_type_filter.h                                                  */
/**************************************************************************/

#pragma once

#include "core/string/string_name.h"

namespace WGodotTypeFilter {

bool is_known(const String &p_filter);
bool matches(const StringName &p_candidate, const StringName &p_filter);

} // namespace WGodotTypeFilter
