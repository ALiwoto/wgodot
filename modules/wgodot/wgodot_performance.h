// wgodot-changes::file
#pragma once

#include "core/variant/dictionary.h"

namespace WGodotPerformance {

Dictionary execute(uint64_t p_request_id, const String &p_command, const Dictionary &p_options, bool &r_deferred);
void end_frame();
void reset();

} // namespace WGodotPerformance
