// wgodot-changes::file
/**************************************************************************/
/*  wgodot_log_service.h                                                  */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotLogService {

void capture_debugger_message(int p_session, const String &p_message, const Array &p_data);
void clear_debugger_session(int p_session);
void reset();

Dictionary get_logs(const Dictionary &p_options);
Dictionary clear_logs(const Dictionary &p_options);

} // namespace WGodotLogService
