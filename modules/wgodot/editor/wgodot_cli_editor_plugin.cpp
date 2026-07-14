// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli_editor_plugin.cpp                                          */
/**************************************************************************/

#include "wgodot_cli_editor_plugin.h"

#include "../wgodot_cli.h"
#include "../wgodot_member_list.h"
#include "wgodot_cli_debugger_bridge.h"
#include "wgodot_debug_service.h"
#include "wgodot_log_service.h"
#include "wgodot_project_info.h"
#include "wgodot_source_info.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/run/editor_run_bar.h"

namespace {

constexpr uint64_t CONNECTION_TIMEOUT_MSEC = 5000;
constexpr uint64_t ASYNC_TIMEOUT_MSEC = 15000;
constexpr uint64_t WAIT_THROUGH_BREAKPOINT_TIMEOUT_MSEC = 60000;
constexpr int MAX_PACKET_SIZE = 4 * 1024 * 1024;
constexpr const char *const FORWARDED_GAME_COMMANDS[] = {
	"tree",
	"ss",
	"observe",
	"click",
	"type",
	"key",
	"action",
	"get",
	"set",
	"call",
	"get_static",
	"set_static",
	"call_static",
	"list",
	"wait",
	"pause",
	"resume",
	"step",
	"pause_physics",
	"resume_physics",
	"step_physics",
};

Dictionary make_error_response(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = p_error;
	response["message"] = p_message;
	response["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	return response;
}

bool is_forwarded_game_command(const String &p_command) {
	for (const char *command : FORWARDED_GAME_COMMANDS) {
		if (p_command == command) {
			return true;
		}
	}
	return false;
}

} // namespace

void WGodotCLIEditorPlugin::_bind_methods() {
}

String WGodotCLIEditorPlugin::generate_random_hex(int p_byte_count) {
	Vector<uint8_t> random_bytes;
	random_bytes.resize(p_byte_count);

	CryptoCore::RandomGenerator random;
	if (random.init() != OK || random.get_random_bytes(random_bytes.ptrw(), random_bytes.size()) != OK) {
		return String();
	}
	return String::hex_encode_buffer(random_bytes.ptr(), random_bytes.size());
}

bool WGodotCLIEditorPlugin::secure_token_matches(const String &p_expected, const String &p_received) {
	const CharString expected = p_expected.utf8();
	const CharString received = p_received.utf8();
	const int expected_length = expected.length();
	const int received_length = received.length();
	uint32_t difference = static_cast<uint32_t>(expected_length ^ received_length);
	for (int i = 0; i < expected_length; i++) {
		const uint8_t received_byte = i < received_length ? static_cast<uint8_t>(received[i]) : 0;
		difference |= static_cast<uint8_t>(expected[i]) ^ received_byte;
	}
	return difference == 0;
}

bool WGodotCLIEditorPlugin::start_server() {
	project_root = WGodotCLI::get_current_project_root();
	if (project_root.is_empty()) {
		return false;
	}
	project_key = WGodotCLI::get_project_key(project_root);
	token = generate_random_hex(32);
	instance_id = generate_random_hex(16);
	if (token.is_empty() || instance_id.is_empty()) {
		ERR_PRINT("WGodot CLI server could not generate its authentication token.");
		return false;
	}

	server.instantiate();
	const Error listen_error = server->listen(0, IPAddress("127.0.0.1"));
	if (listen_error != OK) {
		ERR_PRINT("WGodot CLI server could not listen on the loopback interface.");
		server.unref();
		return false;
	}

	const String discovery_directory = WGodotCLI::get_project_agents_directory(project_key);
	if (DirAccess::make_dir_recursive_absolute(discovery_directory) != OK) {
		ERR_PRINT("WGodot CLI server could not create its discovery directory.");
		server->stop();
		server.unref();
		return false;
	}

	Dictionary record;
	record["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	record["project_root"] = project_root;
	record["project_key"] = project_key;
	record["instance_id"] = instance_id;
	record["host"] = "127.0.0.1";
	record["port"] = server->get_local_port();
	record["token"] = token;
	record["pid"] = OS::get_singleton()->get_process_id();
	record["started_at"] = static_cast<int64_t>(Time::get_singleton()->get_unix_time_from_system() * 1000000.0);

	discovery_file = discovery_directory.path_join(instance_id + ".json");
	const String temporary_discovery_file = discovery_file + ".tmp";
	Ref<FileAccess> file = FileAccess::open(temporary_discovery_file, FileAccess::WRITE);
	if (file.is_null()) {
		ERR_PRINT("WGodot CLI server could not write its discovery record.");
		server->stop();
		server.unref();
		discovery_file.clear();
		return false;
	}
	file->store_string(JSON::stringify(record, "", true));
	file->flush();
	file.unref();
	if (DirAccess::rename_absolute(temporary_discovery_file, discovery_file) != OK) {
		ERR_PRINT("WGodot CLI server could not publish its discovery record.");
		DirAccess::remove_absolute(temporary_discovery_file);
		server->stop();
		server.unref();
		discovery_file.clear();
		return false;
	}

	return true;
}

void WGodotCLIEditorPlugin::stop_server() {
	connections.clear();
	game_session_states.clear();
	if (debugger_bridge.is_valid()) {
		if (EditorDebuggerNode::get_singleton()) {
			EditorDebuggerNode::get_singleton()->remove_debugger_plugin(debugger_bridge);
		}
		debugger_bridge.unref();
	}
	if (server.is_valid()) {
		server->stop();
		server.unref();
	}
	if (!discovery_file.is_empty() && FileAccess::exists(discovery_file)) {
		DirAccess::remove_absolute(discovery_file);
	}
	discovery_file.clear();
	token.clear();
}

void WGodotCLIEditorPlugin::accept_connections() {
	if (server.is_null()) {
		return;
	}
	while (server->is_connection_available()) {
		Ref<StreamPeerTCP> tcp = server->take_connection();
		if (tcp.is_null()) {
			break;
		}

		PendingConnection connection;
		connection.tcp = tcp;
		connection.packet.instantiate();
		connection.packet->set_input_buffer_max_size(MAX_PACKET_SIZE);
		connection.packet->set_output_buffer_max_size(MAX_PACKET_SIZE);
		connection.packet->set_stream_peer(tcp);
		connection.accepted_at_msec = OS::get_singleton()->get_ticks_msec();
		connection.deadline_msec = connection.accepted_at_msec + CONNECTION_TIMEOUT_MSEC;
		connections.push_back(connection);
	}
}

Dictionary WGodotCLIEditorPlugin::make_status_response(const Dictionary &p_request) const {
	Dictionary response;
	response["ok"] = true;
	response["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	response["project_root"] = project_root;
	response["editor_pid"] = OS::get_singleton()->get_process_id();
	WGodotProjectInfo::add_status_fields(response);

	Array sessions;
	int active_session_count = 0;
	int only_active_session = -1;
	int current_session = -1;
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	ScriptEditorDebugger *current_debugger = debugger_node ? debugger_node->get_current_debugger() : nullptr;
	if (debugger_node) {
		for (int id = 0;; id++) {
			ScriptEditorDebugger *debugger = debugger_node->get_debugger(id);
			if (debugger == nullptr) {
				break;
			}

			const bool active = debugger->is_session_active();
			const int64_t remote_pid = debugger->get_remote_pid();
			Dictionary session;
			session["id"] = id;
			session["active"] = active;
			session["selected"] = debugger == current_debugger;
			session["pid"] = remote_pid;
			session["paused"] = debugger->is_breaked();
			session["errors"] = debugger->get_error_count();
			session["warnings"] = debugger->get_warning_count();

			const GameSessionState *game_state = game_session_states.getptr(id);
			const bool has_matching_game_state = game_state && game_state->pid == remote_pid;
			session["game_paused"] = has_matching_game_state && game_state->game_paused;
			session["physics_paused"] = has_matching_game_state && game_state->physics_paused;
			session["physics_effectively_paused"] = has_matching_game_state && game_state->physics_effectively_paused;
			sessions.push_back(session);

			if (debugger == current_debugger) {
				current_session = id;
			}
			if (active) {
				active_session_count++;
				only_active_session = id;
			}
		}
	}

	int automatic_session = -1;
	String selection_reason = "no_active_session";
	if (p_request.has("session")) {
		const int requested_session = p_request["session"];
		ScriptEditorDebugger *requested_debugger = debugger_node ? debugger_node->get_debugger(requested_session) : nullptr;
		if (requested_debugger == nullptr || !requested_debugger->is_session_active()) {
			return make_error_response("invalid_session", vformat("Session %d is not active.", requested_session));
		}
		automatic_session = requested_session;
		selection_reason = "requested";
	} else if (current_session >= 0 && current_debugger->is_session_active()) {
		automatic_session = current_session;
		selection_reason = "editor_selected";
	} else if (active_session_count == 1) {
		automatic_session = only_active_session;
		selection_reason = "only_active";
	} else if (active_session_count > 1) {
		selection_reason = "ambiguous";
	}

	response["sessions"] = sessions;
	response["session_count"] = sessions.size();
	response["active_sessions"] = active_session_count;
	response["game_running"] = active_session_count > 0;
	response["automatic_session"] = automatic_session;
	response["session_selection"] = selection_reason;
	return response;
}

int WGodotCLIEditorPlugin::get_automatic_session(const Dictionary &p_request, Dictionary &r_error) const {
	const Dictionary status = make_status_response(p_request);
	if (!(bool)status.get("ok", false)) {
		r_error = status;
		return -1;
	}

	const int session = status.get("automatic_session", -1);
	if (session >= 0) {
		return session;
	}
	if ((int)status.get("active_sessions", 0) > 1) {
		r_error = make_error_response("ambiguous_session", "More than one game is running and no active session is selected.");
	} else {
		r_error = make_error_response("game_not_running", "No game is running for this editor.");
	}
	return -1;
}

void WGodotCLIEditorPlugin::finish_connection(PendingConnection &p_connection, const Dictionary &p_response) {
	if (p_connection.completed) {
		return;
	}
	const PackedByteArray response_bytes = JSON::stringify(p_response, "", true).to_utf8_buffer();
	p_connection.packet->put_packet(response_bytes.ptr(), response_bytes.size());
	p_connection.tcp->disconnect_from_host();
	p_connection.completed = true;
}

void WGodotCLIEditorPlugin::process_request(PendingConnection &p_connection) {
	const uint8_t *buffer = nullptr;
	int buffer_size = 0;
	if (p_connection.packet->get_packet(&buffer, buffer_size) != OK || buffer_size <= 0 || buffer_size > MAX_PACKET_SIZE) {
		p_connection.tcp->disconnect_from_host();
		p_connection.completed = true;
		return;
	}

	Dictionary request;
	JSON json;
	if (json.parse(String::utf8(reinterpret_cast<const char *>(buffer), buffer_size)) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
		request = json.get_data();
	}

	const String received_token = request.get("token", String());
	if (!secure_token_matches(token, received_token) || String(request.get("project_key", String())) != project_key) {
		finish_connection(p_connection, make_error_response("authentication_failed", "Authentication failed."));
		return;
	} else if ((int)request.get("protocol", 0) != WGodotCLI::PROTOCOL_VERSION) {
		finish_connection(p_connection, make_error_response("protocol_mismatch", "The CLI and editor protocol versions do not match."));
		return;
	}

	const String command = request.get("command", String());
	const Dictionary options = request.get("options", Dictionary());
	if (command == "status") {
		finish_connection(p_connection, make_status_response(request));
		return;
	}
	if (command == "run") {
		const String mode = options.get("mode", "main");
		if (mode != "main" && mode != "current" && mode != "custom") {
			finish_connection(p_connection, make_error_response("invalid_run_mode", "Unknown run mode: " + mode));
			return;
		}
		game_session_states.clear();
		if (mode == "main") {
			EditorRunBar::get_singleton()->play_main_scene(false);
		} else if (mode == "current") {
			EditorRunBar::get_singleton()->play_current_scene(false);
		} else {
			EditorRunBar::get_singleton()->play_custom_scene(options.get("scene", String()));
		}
		if (!EditorRunBar::get_singleton()->is_playing()) {
			finish_connection(p_connection, make_error_response("game_start_failed", "The editor did not start the game."));
			return;
		}
		p_connection.wait_kind = PendingConnection::WAIT_GAME_START;
		p_connection.deadline_msec = OS::get_singleton()->get_ticks_msec() + ASYNC_TIMEOUT_MSEC;
		return;
	}
	if (command == "stop") {
		const bool was_playing = EditorRunBar::get_singleton()->is_playing();
		EditorRunBar::get_singleton()->stop_playing();
		game_session_states.clear();
		Dictionary response;
		response["ok"] = true;
		response["command"] = "stop";
		response["was_running"] = was_playing;
		finish_connection(p_connection, response);
		return;
	}
	if (command == "source_info") {
		finish_connection(p_connection, WGodotSourceInfo::resolve(options));
		return;
	}
	if (command == "rename_preflight") {
		finish_connection(p_connection, WGodotSourceInfo::rename_preflight(options));
		return;
	}
	if (command == "rename_complete") {
		finish_connection(p_connection, WGodotSourceInfo::rename_complete());
		return;
	}
	if (command == "logs") {
		finish_connection(p_connection, WGodotLogService::get_logs(options));
		return;
	}
	if (command == "clear_logs") {
		finish_connection(p_connection, WGodotLogService::clear_logs(options));
		return;
	}
	if (command == "breakpoint") {
		finish_connection(p_connection, WGodotDebugService::execute_breakpoint(options));
		return;
	}
	if (command == "debug") {
		const String action = options.get("action", String());
		Dictionary session_error;
		int session = get_automatic_session(request, session_error);
		if (session < 0 && action == "state" && String(session_error.get("error", String())) == "game_not_running") {
			finish_connection(p_connection, WGodotDebugService::get_state(-1, "state"));
			return;
		}
		if (session < 0) {
			finish_connection(p_connection, session_error);
			return;
		}
		const int timeout_msec = options.get("timeout_msec", 15000);
		if (timeout_msec <= 0 || timeout_msec > 60000) {
			finish_connection(p_connection, make_error_response("invalid_timeout", "Debug timeout must be between 1 and 60000 milliseconds."));
			return;
		}
		WGodotDebugService::WaitKind wait_kind = WGodotDebugService::WAIT_NONE;
		uint64_t generation = 0;
		const Dictionary response = WGodotDebugService::execute_debug(session, options, wait_kind, generation);
		if (!(bool)response.get("ok", true) || wait_kind == WGodotDebugService::WAIT_NONE) {
			finish_connection(p_connection, response);
			return;
		}
		p_connection.game_session = session;
		p_connection.debug_options = options;
		p_connection.debug_wait_kind = wait_kind;
		p_connection.debug_generation = generation;
		p_connection.wait_kind = PendingConnection::WAIT_DEBUG;
		p_connection.deadline_msec = OS::get_singleton()->get_ticks_msec() + timeout_msec;
		return;
	}
	if (is_forwarded_game_command(command)) {
		Dictionary session_error;
		const int session = get_automatic_session(request, session_error);
		if (session < 0) {
			if (command == "list" && String(session_error.get("error", String())) == "game_not_running") {
				Dictionary metadata_options = options;
				metadata_options["metadata_only"] = true;
				finish_connection(p_connection, WGodotMemberList::execute(metadata_options));
				return;
			}
			finish_connection(p_connection, session_error);
			return;
		}
		const uint64_t game_request_id = next_game_request_id++;
		if (debugger_bridge.is_null() || !debugger_bridge->send_request(session, game_request_id, command, options)) {
			if (command == "list") {
				Dictionary metadata_options = options;
				metadata_options["metadata_only"] = true;
				finish_connection(p_connection, WGodotMemberList::execute(metadata_options));
				return;
			}
			finish_connection(p_connection, make_error_response("game_request_failed", "Could not send the request to the running game."));
			return;
		}
		const bool call_command = command == "call" || command == "call_static";
		if (call_command && (bool)options.get("detach", false)) {
			Dictionary response;
			response["ok"] = true;
			response["command"] = command;
			response["completed"] = false;
			response["detached"] = true;
			response["state"] = "dispatched";
			response["request_id"] = static_cast<int64_t>(game_request_id);
			response["session"] = session;
			finish_connection(p_connection, response);
			return;
		}
		p_connection.game_request_id = game_request_id;
		p_connection.game_session = session;
		p_connection.game_command = command;
		p_connection.return_on_debug_break = call_command && !(bool)options.get("wait_through_breakpoint", false);
		p_connection.wait_kind = PendingConnection::WAIT_GAME_RESPONSE;
		p_connection.deadline_msec = OS::get_singleton()->get_ticks_msec() + ((bool)options.get("wait_through_breakpoint", false) ? WAIT_THROUGH_BREAKPOINT_TIMEOUT_MSEC : ASYNC_TIMEOUT_MSEC);
		return;
	}

	finish_connection(p_connection, make_error_response("unknown_command", "Unknown WGodot editor command: " + command));
}

void WGodotCLIEditorPlugin::poll_waiting_connection(PendingConnection &p_connection) {
	if (p_connection.wait_kind == PendingConnection::WAIT_GAME_RESPONSE) {
		if (p_connection.return_on_debug_break && p_connection.game_debug_break_observed) {
			const Dictionary debug_state = WGodotDebugService::get_state(p_connection.game_session, p_connection.game_command);
			if ((bool)debug_state.get("breaked", false) && (bool)debug_state.get("stack_ready", false)) {
				Dictionary response;
				response["ok"] = true;
				response["command"] = p_connection.game_command;
				response["completed"] = false;
				response["detached"] = false;
				response["state"] = "breaked";
				response["request_id"] = static_cast<int64_t>(p_connection.game_request_id);
				response["session"] = p_connection.game_session;
				response["reason"] = debug_state.get("reason", String());
				response["frame"] = debug_state.get("frame", Dictionary());
				finish_connection(p_connection, response);
			}
		}
		return;
	}
	if (p_connection.wait_kind == PendingConnection::WAIT_DEBUG) {
		Dictionary response;
		WGodotDebugService::WaitKind wait_kind = static_cast<WGodotDebugService::WaitKind>(p_connection.debug_wait_kind);
		if (WGodotDebugService::poll_debug_wait(p_connection.game_session, p_connection.debug_options, wait_kind, p_connection.debug_generation, response)) {
			finish_connection(p_connection, response);
		} else {
			p_connection.debug_wait_kind = wait_kind;
		}
		return;
	}
	if (p_connection.wait_kind != PendingConnection::WAIT_GAME_START) {
		return;
	}

	Dictionary session_error;
	const int session = get_automatic_session(Dictionary(), session_error);
	if (session < 0) {
		return;
	}
	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_debugger(session);
	if (!debugger || debugger->get_remote_pid() == 0) {
		return;
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "run";
	response["scene"] = EditorRunBar::get_singleton()->get_playing_scene();
	response["session"] = session;
	response["pid"] = debugger->get_remote_pid();
	finish_connection(p_connection, response);
}

void WGodotCLIEditorPlugin::handle_game_response(int p_session, uint64_t p_request_id, const Dictionary &p_response) {
	if (p_response.has("paused") && p_response.has("physics_paused") && p_response.has("physics_effectively_paused")) {
		ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_debugger(p_session);
		GameSessionState state;
		state.pid = debugger ? debugger->get_remote_pid() : 0;
		state.game_paused = p_response["paused"];
		state.physics_paused = p_response["physics_paused"];
		state.physics_effectively_paused = p_response["physics_effectively_paused"];
		game_session_states[p_session] = state;
	}

	for (PendingConnection &connection : connections) {
		if (!connection.completed && connection.wait_kind == PendingConnection::WAIT_GAME_RESPONSE && connection.game_request_id == p_request_id && connection.game_session == p_session) {
			Dictionary response = p_response;
			response["session"] = p_session;
			if (connection.game_command == "call" || connection.game_command == "call_static") {
				response["completed"] = true;
				response["detached"] = false;
			}
			finish_connection(connection, response);
			return;
		}
	}
}

void WGodotCLIEditorPlugin::setup_debugger_session(int p_session) {
	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_debugger(p_session);
	if (debugger == nullptr) {
		return;
	}
	const Callable data_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_debugger_data).bind(p_session);
	if (!debugger->is_connected("debug_data", data_callback)) {
		debugger->connect("debug_data", data_callback);
	}
	const Callable clear_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_debugger_errors_cleared).bind(p_session);
	if (!debugger->is_connected("errors_cleared", clear_callback)) {
		debugger->connect("errors_cleared", clear_callback);
	}
	const Callable started_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_debugger_started).bind(p_session);
	if (!debugger->is_connected("started", started_callback)) {
		debugger->connect("started", started_callback);
	}
	const Callable stopped_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_debugger_stopped).bind(p_session);
	if (!debugger->is_connected("stopped", stopped_callback)) {
		debugger->connect("stopped", stopped_callback);
	}
	const Callable breaked_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_debugger_breaked).bind(p_session);
	if (!debugger->is_connected("breaked", breaked_callback)) {
		debugger->connect("breaked", breaked_callback);
	}
	if (debugger->is_session_active()) {
		WGodotDebugService::debugger_started(p_session);
	}
}

void WGodotCLIEditorPlugin::handle_debugger_data(const String &p_message, const Array &p_data, int p_session) {
	WGodotLogService::capture_debugger_message(p_session, p_message, p_data);
	WGodotDebugService::capture_debugger_message(p_session, p_message, p_data);
}

void WGodotCLIEditorPlugin::handle_debugger_errors_cleared(int p_session) {
	WGodotLogService::clear_debugger_session(p_session);
}

void WGodotCLIEditorPlugin::handle_debugger_started(int p_session) {
	WGodotDebugService::debugger_started(p_session);
}

void WGodotCLIEditorPlugin::handle_debugger_stopped(int p_session) {
	WGodotDebugService::debugger_stopped(p_session);
}

void WGodotCLIEditorPlugin::handle_debugger_breaked(bool p_breaked, bool p_can_debug, const String &p_reason, bool p_has_stackdump, int p_session) {
	WGodotDebugService::debugger_breaked(p_session, p_breaked, p_can_debug, p_reason, p_has_stackdump);
	if (p_breaked) {
		for (PendingConnection &connection : connections) {
			if (!connection.completed && connection.wait_kind == PendingConnection::WAIT_GAME_RESPONSE && connection.game_session == p_session && connection.return_on_debug_break) {
				connection.game_debug_break_observed = true;
			}
		}
	}
}

void WGodotCLIEditorPlugin::handle_breakpoint_toggled(const String &p_path, int p_line, bool p_enabled) {
	WGodotDebugService::sync_breakpoint(p_path, p_line, p_enabled);
}

void WGodotCLIEditorPlugin::poll_connections() {
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	for (int i = connections.size() - 1; i >= 0; i--) {
		PendingConnection &connection = connections.write[i];
		connection.tcp->poll();
		if (connection.completed) {
			connections.remove_at(i);
			continue;
		}
		if (connection.wait_kind == PendingConnection::WAIT_NONE && connection.packet->get_available_packet_count() > 0) {
			process_request(connection);
		} else if (connection.wait_kind != PendingConnection::WAIT_NONE) {
			poll_waiting_connection(connection);
		}
		if (connection.completed) {
			connections.remove_at(i);
		} else if (connection.tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
			connection.tcp->disconnect_from_host();
			connections.remove_at(i);
		} else if (now > connection.deadline_msec) {
			if (connection.wait_kind == PendingConnection::WAIT_DEBUG) {
				finish_connection(connection, make_error_response("debug_timeout", "Timed out while waiting for the debugger state transition."));
			} else {
				finish_connection(connection, make_error_response("timeout", "The WGodot command timed out."));
			}
			connections.remove_at(i);
		}
	}
}

void WGodotCLIEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			WGodotLogService::reset();
			WGodotDebugService::initialize();
			const Callable breakpoint_callback = callable_mp(this, &WGodotCLIEditorPlugin::handle_breakpoint_toggled);
			if (!EditorDebuggerNode::get_singleton()->is_connected("breakpoint_toggled", breakpoint_callback)) {
				EditorDebuggerNode::get_singleton()->connect("breakpoint_toggled", breakpoint_callback);
			}
			debugger_bridge = Ref<WGodotCLIDebuggerBridge>(memnew(WGodotCLIDebuggerBridge(this)));
			EditorDebuggerNode::get_singleton()->add_debugger_plugin(debugger_bridge);
			if (start_server()) {
				set_process(true);
			}
		} break;
		case NOTIFICATION_PROCESS: {
			accept_connections();
			poll_connections();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process(false);
			stop_server();
			WGodotLogService::reset();
			WGodotDebugService::reset();
		} break;
	}
}

WGodotCLIEditorPlugin::WGodotCLIEditorPlugin() {
}

WGodotCLIEditorPlugin::~WGodotCLIEditorPlugin() {
	stop_server();
}
