// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli_editor_plugin.h                                            */
/**************************************************************************/

#pragma once

#include "core/io/packet_peer.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "editor/plugins/editor_plugin.h"

class WGodotCLIDebuggerBridge;

class WGodotCLIEditorPlugin : public EditorPlugin {
	GDCLASS(WGodotCLIEditorPlugin, EditorPlugin);

	struct PendingConnection {
		enum WaitKind {
			WAIT_NONE,
			WAIT_GAME_START,
			WAIT_GAME_RESPONSE,
			WAIT_DEBUG,
		};

		Ref<StreamPeerTCP> tcp;
		Ref<PacketPeerStream> packet;
		uint64_t accepted_at_msec = 0;
		uint64_t deadline_msec = 0;
		uint64_t game_request_id = 0;
		uint64_t debug_generation = 0;
		int game_session = -1;
		int debug_wait_kind = 0;
		String debug_action;
		WaitKind wait_kind = WAIT_NONE;
		bool completed = false;
	};

	struct GameSessionState {
		int64_t pid = 0;
		bool game_paused = false;
		bool physics_paused = false;
		bool physics_effectively_paused = false;
	};

	Ref<TCPServer> server;
	Ref<WGodotCLIDebuggerBridge> debugger_bridge;
	Vector<PendingConnection> connections;
	HashMap<int, GameSessionState> game_session_states;
	uint64_t next_game_request_id = 1;
	String token;
	String instance_id;
	String project_root;
	String project_key;
	String discovery_file;

	bool start_server();
	void stop_server();
	void accept_connections();
	void poll_connections();
	void process_request(PendingConnection &p_connection);
	void finish_connection(PendingConnection &p_connection, const Dictionary &p_response);
	void poll_waiting_connection(PendingConnection &p_connection);
	Dictionary make_status_response(const Dictionary &p_request) const;
	int get_automatic_session(const Dictionary &p_request, Dictionary &r_error) const;

	static bool secure_token_matches(const String &p_expected, const String &p_received);
	static String generate_random_hex(int p_byte_count);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void handle_game_response(int p_session, uint64_t p_request_id, const Dictionary &p_response);
	void setup_debugger_session(int p_session);
	void handle_debugger_data(const String &p_message, const Array &p_data, int p_session);
	void handle_debugger_errors_cleared(int p_session);
	void handle_debugger_started(int p_session);
	void handle_debugger_stopped(int p_session);
	void handle_debugger_breaked(bool p_breaked, bool p_can_debug, const String &p_reason, bool p_has_stackdump, int p_session);
	void handle_breakpoint_toggled(const String &p_path, int p_line, bool p_enabled);

	WGodotCLIEditorPlugin();
	~WGodotCLIEditorPlugin();
};
