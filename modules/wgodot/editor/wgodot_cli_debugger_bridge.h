// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli_debugger_bridge.h                                          */
/**************************************************************************/

#pragma once

#include "editor/debugger/editor_debugger_plugin.h"

class WGodotCLIEditorPlugin;

class WGodotCLIDebuggerBridge : public EditorDebuggerPlugin {
	GDCLASS(WGodotCLIDebuggerBridge, EditorDebuggerPlugin);

	WGodotCLIEditorPlugin *owner = nullptr;

protected:
	static void _bind_methods();

public:
	virtual void setup_session(int p_session) override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session) override;
	virtual bool has_capture(const String &p_capture) const override;

	bool send_request(int p_session, uint64_t p_request_id, const String &p_command, const Dictionary &p_options);

	explicit WGodotCLIDebuggerBridge(WGodotCLIEditorPlugin *p_owner);
};
