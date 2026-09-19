// wgodot-changes::file
/**************************************************************************/
/*  export_timing.h                                                       */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "core/string/ustring.h"
#include "core/typedefs.h"

namespace WGodotGDScriptExportTransform {

uint64_t export_timing_get_ticks_usec();
String export_timing_format_msec(uint64_t p_usec);
String export_timing_prefix();
bool export_timing_should_log(const TransformOptions &p_options);
bool export_timing_should_log_verbose(const TransformOptions &p_options);
uint64_t export_timing_slow_threshold_usec(const TransformOptions &p_options);
void export_timing_log_checkpoint(const TransformOptions &p_options, const String &p_context, const String &p_phase);
void export_timing_log_slow_phase(const TransformOptions &p_options, const String &p_context, const String &p_phase, uint64_t p_usec, uint64_t p_threshold_usec = 0);

} // namespace WGodotGDScriptExportTransform
