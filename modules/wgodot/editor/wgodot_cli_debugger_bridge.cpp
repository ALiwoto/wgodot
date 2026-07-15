// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli_debugger_bridge.cpp                                        */
/**************************************************************************/

#include "wgodot_cli_debugger_bridge.h"

#include "wgodot_cli_editor_plugin.h"

void WGodotCLIDebuggerBridge::_bind_methods() {
}

void WGodotCLIDebuggerBridge::setup_session(int p_session) {
	if (owner) {
		owner->setup_debugger_session(p_session);
	}
}

bool WGodotCLIDebuggerBridge::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message == "wgodot:conditional_breakpoint_hit") {
		return true;
	}
	if (p_message != "wgodot:response" || p_data.size() != 2 || p_data[0].get_type() != Variant::INT || p_data[1].get_type() != Variant::DICTIONARY) {
		return false;
	}
	if (owner) {
		owner->handle_game_response(p_session, p_data[0], p_data[1]);
	}
	return true;
}

bool WGodotCLIDebuggerBridge::has_capture(const String &p_capture) const {
	return p_capture == "wgodot";
}

bool WGodotCLIDebuggerBridge::send_request(int p_session, uint64_t p_request_id, const String &p_command, const Dictionary &p_options) {
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (session.is_null() || !session->is_active()) {
		return false;
	}
	session->send_message("wgodot:request", { p_request_id, p_command, p_options });
	return true;
}

WGodotCLIDebuggerBridge::WGodotCLIDebuggerBridge(WGodotCLIEditorPlugin *p_owner) {
	owner = p_owner;
}
