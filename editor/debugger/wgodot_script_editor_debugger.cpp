// wgodot-changes::file

#include "script_editor_debugger.h"

void ScriptEditorDebugger::wgodot_send_debug_message(const String &p_message, const Array &p_args) {
	_put_msg(p_message, p_args, debugging_thread_id != Thread::UNASSIGNED_ID ? debugging_thread_id : Thread::MAIN_ID);
}
