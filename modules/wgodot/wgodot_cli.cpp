// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli.cpp                                                        */
/**************************************************************************/

#include "wgodot_cli.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/packet_peer.h"
#include "core/io/stream_peer_tcp.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/vector.h"
#include "editor/file_system/editor_paths.h"
#include "modules/gdscript/wgodot_gd/gdscript_check_cli.h"

namespace WGodotCLI {

namespace {

constexpr uint64_t CONNECT_TIMEOUT_MSEC = 750;
constexpr uint64_t RESPONSE_TIMEOUT_MSEC = 3000;
constexpr int MAX_PACKET_SIZE = 1024 * 1024;

bool command_requested = false;
Vector<String> command_arguments;

struct EditorRecord {
	String file_path;
	String project_root;
	String project_key;
	String instance_id;
	String host;
	String token;
	int port = 0;
	int64_t pid = 0;
	int64_t started_at = 0;
};

String find_project_root_upwards() {
	Ref<DirAccess> dir = DirAccess::open(OS::get_singleton()->get_cwd());
	if (dir.is_null()) {
		return String();
	}

	String current_dir = dir->get_current_dir().replace_char('\\', '/').simplify_path();
	while (!current_dir.is_empty()) {
		if (FileAccess::exists(current_dir.path_join("project.godot"))) {
			return current_dir;
		}

		const String parent_dir = current_dir.get_base_dir();
		if (parent_dir == current_dir) {
			break;
		}
		current_dir = parent_dir;
	}

	return String();
}

bool load_editor_record(const String &p_path, EditorRecord &r_record) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		return false;
	}

	JSON json;
	if (json.parse(source) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}

	const Dictionary data = json.get_data();
	if ((int)data.get("protocol", 0) != PROTOCOL_VERSION) {
		return false;
	}

	r_record.file_path = p_path;
	r_record.project_root = data.get("project_root", String());
	r_record.project_key = data.get("project_key", String());
	r_record.instance_id = data.get("instance_id", String());
	r_record.host = data.get("host", String());
	r_record.token = data.get("token", String());
	r_record.port = data.get("port", 0);
	r_record.pid = data.get("pid", 0);
	r_record.started_at = data.get("started_at", 0);

	return !r_record.project_key.is_empty() && !r_record.instance_id.is_empty() &&
			r_record.host == "127.0.0.1" && r_record.token.length() == 64 &&
			r_record.port > 0 && r_record.port <= 65535 && r_record.pid > 0;
}

void collect_records_from_directory(const String &p_directory, Vector<EditorRecord> &r_records) {
	if (!DirAccess::dir_exists_absolute(p_directory)) {
		return;
	}
	for (const String &file_name : DirAccess::get_files_at(p_directory)) {
		if (!file_name.ends_with(".json")) {
			continue;
		}

		EditorRecord record;
		if (!load_editor_record(p_directory.path_join(file_name), record)) {
			continue;
		}
		if (!OS::get_singleton()->is_process_running(record.pid)) {
			continue;
		}
		r_records.push_back(record);
	}
}

Vector<EditorRecord> find_editor_records(bool &r_project_was_resolved) {
	Vector<EditorRecord> records;
	const String project_root = get_current_project_root();
	r_project_was_resolved = !project_root.is_empty();

	if (r_project_was_resolved) {
		collect_records_from_directory(get_project_agents_directory(get_project_key(project_root)), records);
		return records;
	}

	const String agents_root = get_agents_root_directory();
	if (!DirAccess::dir_exists_absolute(agents_root)) {
		return records;
	}
	for (const String &project_directory : DirAccess::get_directories_at(agents_root)) {
		collect_records_from_directory(agents_root.path_join(project_directory), records);
	}
	return records;
}

bool receive_response(const Ref<StreamPeerTCP> &p_tcp, const Ref<PacketPeerStream> &p_packet, Dictionary &r_response) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + RESPONSE_TIMEOUT_MSEC;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		p_tcp->poll();
		if (p_packet->get_available_packet_count() > 0) {
			const uint8_t *buffer = nullptr;
			int buffer_size = 0;
			if (p_packet->get_packet(&buffer, buffer_size) != OK || buffer_size <= 0) {
				return false;
			}

			JSON json;
			if (json.parse(String::utf8(reinterpret_cast<const char *>(buffer), buffer_size)) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
				return false;
			}
			r_response = json.get_data();
			return true;
		}

		if (p_tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
			return false;
		}
		OS::get_singleton()->delay_usec(100);
	}
	return false;
}

bool send_request(const EditorRecord &p_record, const Dictionary &p_request, Dictionary &r_response) {
	Ref<StreamPeerTCP> tcp;
	tcp.instantiate();
	if (tcp->connect_to_host(IPAddress(p_record.host), p_record.port) != OK) {
		return false;
	}

	const uint64_t connect_deadline = OS::get_singleton()->get_ticks_msec() + CONNECT_TIMEOUT_MSEC;
	while (tcp->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_msec() < connect_deadline) {
		tcp->poll();
		OS::get_singleton()->delay_usec(100);
	}
	if (tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return false;
	}

	Ref<PacketPeerStream> packet;
	packet.instantiate();
	packet->set_input_buffer_max_size(MAX_PACKET_SIZE);
	packet->set_output_buffer_max_size(MAX_PACKET_SIZE);
	packet->set_stream_peer(tcp);

	Dictionary authenticated_request = p_request;
	authenticated_request["token"] = p_record.token;
	authenticated_request["project_key"] = p_record.project_key;
	const PackedByteArray request_bytes = JSON::stringify(authenticated_request, "", true).to_utf8_buffer();
	if (packet->put_packet(request_bytes.ptr(), request_bytes.size()) != OK) {
		return false;
	}

	return receive_response(tcp, packet, r_response);
}

void print_cli_help() {
	print_line("Usage: godot [Godot options] --wg <command> [arguments]");
	print_line("");
	print_line("Commands:");
	print_line("  status [--json] [--session <id>]  Show the matching editor and running game sessions.");
	print_line("  check                             Check all project GDScript files.");
	print_line("  help                              Show this help.");
}

void print_status_human(const Dictionary &p_response) {
	print_line("WGodot editor: connected");
	print_line("Project: " + String(p_response.get("project_root", String())));
	print_line(vformat("Editor: PID %d", (int64_t)p_response.get("editor_pid", 0)));

	const int active_sessions = p_response.get("active_sessions", 0);
	const int automatic_session = p_response.get("automatic_session", -1);
	if (active_sessions == 0) {
		print_line("Game: not running");
	} else if (automatic_session >= 0) {
		const Array sessions = p_response.get("sessions", Array());
		int64_t game_pid = 0;
		for (const Variant &session_variant : sessions) {
			const Dictionary session = session_variant;
			if ((int)session.get("id", -1) == automatic_session) {
				game_pid = session.get("pid", 0);
				break;
			}
		}
		print_line(vformat("Game: running (session %d, PID %d)", automatic_session, game_pid));
		if (active_sessions > 1) {
			print_line(vformat("Sessions: %d active; using the editor-selected session", active_sessions));
		}
	} else {
		print_line(vformat("Game: %d sessions are active, but none is selected", active_sessions));
	}
}

int run_status(const Vector<String> &p_arguments) {
	bool json_output = false;
	int requested_session = -1;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--json") {
			json_output = true;
		} else if (argument == "--session") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int()) {
				print_line("wgodot: --session requires a non-negative integer session ID.");
				return 2;
			}
			requested_session = p_arguments[++i].to_int();
			if (requested_session < 0) {
				print_line("wgodot: --session requires a non-negative integer session ID.");
				return 2;
			}
		} else {
			print_line("wgodot: unknown status argument: " + argument);
			return 2;
		}
	}

	bool project_was_resolved = false;
	Vector<EditorRecord> records = find_editor_records(project_was_resolved);
	if (!project_was_resolved && records.size() > 1) {
		Dictionary error;
		error["ok"] = false;
		error["error"] = "multiple_editors";
		error["message"] = "No project.godot was found and more than one WGodot editor is running.";
		if (json_output) {
			print_line(JSON::stringify(error, "", true));
		} else {
			print_line("wgodot: " + String(error["message"]));
		}
		return 3;
	}

	Dictionary request;
	request["protocol"] = PROTOCOL_VERSION;
	request["command"] = "status";
	if (requested_session >= 0) {
		request["session"] = requested_session;
	}

	while (!records.is_empty()) {
		int newest_index = 0;
		for (int i = 1; i < records.size(); i++) {
			if (records[i].started_at > records[newest_index].started_at) {
				newest_index = i;
			}
		}

		Dictionary response;
		if (send_request(records[newest_index], request, response)) {
			if (!(bool)response.get("ok", false) && String(response.get("error", String())) == "authentication_failed") {
				records.remove_at(newest_index);
				continue;
			}
			if (json_output) {
				print_line(JSON::stringify(response, "", true));
			} else if ((bool)response.get("ok", false)) {
				print_status_human(response);
			} else {
				print_line("wgodot: " + String(response.get("message", "The editor rejected the request.")));
			}
			return (bool)response.get("ok", false) ? 0 : 4;
		}
		records.remove_at(newest_index);
	}

	Dictionary error;
	error["ok"] = false;
	error["error"] = "editor_not_found";
	error["message"] = project_was_resolved ? "No running WGodot editor was found for this project." : "No running WGodot editor was found.";
	if (json_output) {
		print_line(JSON::stringify(error, "", true));
	} else {
		print_line("wgodot: " + String(error["message"]));
	}
	return 3;
}

} // namespace

bool extract_arguments(List<String> &r_arguments, String &r_project_path) {
	List<String>::Element *marker = r_arguments.find("--wg");
	if (marker == nullptr) {
		return false;
	}

	command_requested = true;
	command_arguments.clear();
	for (List<String>::Element *argument = marker->next(); argument; argument = argument->next()) {
		command_arguments.push_back(argument->get());
	}

	List<String>::Element *to_remove = marker;
	while (to_remove) {
		List<String>::Element *next = to_remove->next();
		r_arguments.erase(to_remove);
		to_remove = next;
	}

	if (r_project_path == ".") {
		const String discovered_project = find_project_root_upwards();
		if (!discovered_project.is_empty()) {
			r_project_path = discovered_project;
		}
	}

	return true;
}

bool execute_if_requested(int &r_exit_code) {
	if (!command_requested) {
		return false;
	}

	if (command_arguments.is_empty() || command_arguments[0] == "help" || command_arguments[0] == "--help" || command_arguments[0] == "-h") {
		print_cli_help();
		r_exit_code = 0;
		return true;
	}

	const String command = command_arguments[0];
	Vector<String> arguments;
	for (int i = 1; i < command_arguments.size(); i++) {
		arguments.push_back(command_arguments[i]);
	}

	if (command == "check") {
		if (!arguments.is_empty()) {
			print_line("wgodot: check does not accept arguments.");
			r_exit_code = 2;
		} else if (!ProjectSettings::get_singleton()->is_project_loaded()) {
			print_line("wgodot: no project.godot could be found.");
			r_exit_code = 3;
		} else {
			r_exit_code = WGodotGDScriptCheckCLI::run_project_check();
		}
		return true;
	}

	if (command == "status") {
		r_exit_code = run_status(arguments);
		return true;
	}

	print_line("wgodot: unknown command: " + command);
	print_cli_help();
	r_exit_code = 2;
	return true;
}

String get_current_project_root() {
	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		return String();
	}
	return ProjectSettings::get_singleton()->get_resource_path().replace_char('\\', '/').simplify_path();
}

String get_project_key(const String &p_project_root) {
	String normalized_root = p_project_root.replace_char('\\', '/').simplify_path();
#ifdef WINDOWS_ENABLED
	normalized_root = normalized_root.to_lower();
#endif
	return normalized_root.sha256_text();
}

String get_agents_root_directory() {
	ERR_FAIL_NULL_V(EditorPaths::get_singleton(), String());
	return EditorPaths::get_singleton()->get_data_dir().path_join("wgodot").path_join("agents");
}

String get_project_agents_directory(const String &p_project_key) {
	return get_agents_root_directory().path_join(p_project_key);
}

} // namespace WGodotCLI
