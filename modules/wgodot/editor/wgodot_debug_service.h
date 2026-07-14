// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_service.h                                                */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotDebugService {

enum WaitKind {
	WAIT_NONE,
	WAIT_CURRENT_BREAK,
	WAIT_NEXT_BREAK,
	WAIT_RESUME,
	WAIT_VARIABLES,
	WAIT_MEMBERS,
};

void initialize();
void reset();

void sync_breakpoint(const String &p_path, int p_line, bool p_enabled);
Dictionary execute_breakpoint(const Dictionary &p_options);

void debugger_started(int p_session);
void debugger_stopped(int p_session);
void debugger_breaked(int p_session, bool p_breaked, bool p_can_debug, const String &p_reason, bool p_has_stackdump);
void capture_debugger_message(int p_session, const String &p_message, const Array &p_data);

Dictionary get_state(int p_session, const String &p_action);
Dictionary execute_debug(int p_session, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation);
bool poll_debug_wait(int p_session, const Dictionary &p_options, WaitKind &r_wait_kind, uint64_t &r_generation, Dictionary &r_response);
void cancel_debug_wait(int p_session, WaitKind p_wait_kind);

} // namespace WGodotDebugService
