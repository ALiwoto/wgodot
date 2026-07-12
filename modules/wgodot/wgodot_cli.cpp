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
constexpr uint64_t RESPONSE_TIMEOUT_MSEC = 20000;
constexpr int MAX_PACKET_SIZE = 4 * 1024 * 1024;

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
	print_line("  run [--current|<scene>] [--json]  Run the main, current, or specified scene.");
	print_line("  stop [--json]                     Stop the running game.");
	print_line("  tree [options]                    Print the running game's scene tree.");
	print_line("  ss [-o <path>] [--json]           Capture the running game viewport.");
	print_line("  observe [options]                 Capture a screenshot and scene tree together.");
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

int print_status_response(const Dictionary &p_response, bool p_json_output) {
	if (p_json_output) {
		print_line(JSON::stringify(p_response, "", true));
	} else if ((bool)p_response.get("ok", false)) {
		print_status_human(p_response);
	} else {
		print_line("wgodot: " + String(p_response.get("message", "The editor rejected the request.")));
	}
	return (bool)p_response.get("ok", false) ? 0 : 4;
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

	Dictionary request;
	request["protocol"] = PROTOCOL_VERSION;
	request["command"] = "status";
	if (requested_session >= 0) {
		request["session"] = requested_session;
	}

	Dictionary discovered_response;
	int live_editor_count = 0;
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
			if (project_was_resolved) {
				return print_status_response(response, json_output);
			}
			discovered_response = response;
			live_editor_count++;
		}
		records.remove_at(newest_index);
	}

	if (!project_was_resolved && live_editor_count > 1) {
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
	if (!project_was_resolved && live_editor_count == 1) {
		return print_status_response(discovered_response, json_output);
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

int request_editor(const Dictionary &p_request, Dictionary &r_response) {
	bool project_was_resolved = false;
	Vector<EditorRecord> records = find_editor_records(project_was_resolved);
	Dictionary discovered_response;
	int live_editor_count = 0;

	while (!records.is_empty()) {
		int newest_index = 0;
		for (int i = 1; i < records.size(); i++) {
			if (records[i].started_at > records[newest_index].started_at) {
				newest_index = i;
			}
		}

		Dictionary response;
		if (send_request(records[newest_index], p_request, response)) {
			if (!(bool)response.get("ok", false) && String(response.get("error", String())) == "authentication_failed") {
				records.remove_at(newest_index);
				continue;
			}
			if (project_was_resolved) {
				r_response = response;
				return 0;
			}
			discovered_response = response;
			live_editor_count++;
		}
		records.remove_at(newest_index);
	}

	if (!project_was_resolved && live_editor_count == 1) {
		r_response = discovered_response;
		return 0;
	}

	r_response["ok"] = false;
	if (!project_was_resolved && live_editor_count > 1) {
		r_response["error"] = "multiple_editors";
		r_response["message"] = "No project.godot was found and more than one WGodot editor is running.";
	} else {
		r_response["error"] = "editor_not_found";
		r_response["message"] = project_was_resolved ? "No running WGodot editor was found for this project." : "No running WGodot editor was found.";
	}
	return 3;
}

String make_absolute_output_path(const String &p_path) {
	if (p_path.is_empty() || p_path.is_absolute_path() || p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return p_path;
	}
	Ref<DirAccess> directory = DirAccess::open(OS::get_singleton()->get_cwd());
	return directory.is_valid() ? directory->get_current_dir().path_join(p_path).simplify_path() : p_path;
}

struct GameCommandOptions {
	bool json_output = false;
	int session = -1;
	int max_depth = -1;
	String root;
	String output;
	PackedStringArray include_types;
	PackedStringArray exclude_types;
};

bool append_tree_filter_types(const String &p_option, const String &p_value, PackedStringArray &r_types) {
	const PackedStringArray values = p_value.split(",");
	for (int i = 0; i < values.size(); i++) {
		const String type = values[i].strip_edges();
		if (type.is_empty()) {
			print_line("wgodot: " + p_option + " requires one or more comma-separated type names.");
			return false;
		}
		r_types.push_back(type);
	}
	return true;
}

bool parse_game_command_options(const String &p_command, const Vector<String> &p_arguments, bool p_allow_tree_options, bool p_allow_output, GameCommandOptions &r_options) {
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--json") {
			r_options.json_output = true;
		} else if (argument == "--session") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() < 0) {
				print_line("wgodot: --session requires a non-negative integer session ID.");
				return false;
			}
			r_options.session = p_arguments[++i].to_int();
		} else if (p_allow_tree_options && (argument == "--include" || argument == "--exclude")) {
			if (i + 1 >= p_arguments.size()) {
				print_line("wgodot: " + argument + " requires one or more type names.");
				return false;
			}
			PackedStringArray &types = argument == "--include" ? r_options.include_types : r_options.exclude_types;
			if (!append_tree_filter_types(argument, p_arguments[++i], types)) {
				return false;
			}
		} else if (p_allow_tree_options && argument == "--depth") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() < 0) {
				print_line("wgodot: --depth requires a non-negative integer.");
				return false;
			}
			r_options.max_depth = p_arguments[++i].to_int();
		} else if (p_allow_tree_options && argument == "--root") {
			if (i + 1 >= p_arguments.size()) {
				print_line("wgodot: --root requires a node path.");
				return false;
			}
			r_options.root = p_arguments[++i];
		} else if (p_allow_output && (argument == "-o" || argument == "--output")) {
			if (i + 1 >= p_arguments.size()) {
				print_line("wgodot: " + argument + " requires a file path.");
				return false;
			}
			r_options.output = make_absolute_output_path(p_arguments[++i]);
		} else {
			print_line("wgodot: unknown " + p_command + " argument: " + argument);
			return false;
		}
	}
	if (p_allow_tree_options && r_options.include_types.is_empty()) {
		r_options.include_types.push_back("*");
	}
	return true;
}

void add_game_options_to_request(const GameCommandOptions &p_options, Dictionary &r_request) {
	Dictionary options;
	options["max_depth"] = p_options.max_depth;
	if (!p_options.include_types.is_empty()) {
		options["include_types"] = p_options.include_types;
	}
	if (!p_options.exclude_types.is_empty()) {
		options["exclude_types"] = p_options.exclude_types;
	}
	if (!p_options.root.is_empty()) {
		options["root"] = p_options.root;
	}
	if (!p_options.output.is_empty()) {
		options["output"] = p_options.output;
	}
	r_request["options"] = options;
	if (p_options.session >= 0) {
		r_request["session"] = p_options.session;
	}
}

void print_tree(const Array &p_tree) {
	for (const Variant &entry_variant : p_tree) {
		const Dictionary entry = entry_variant;
		const int depth = entry.get("depth", 0);
		print_line(String("  ").repeat(depth) + String(entry.get("name", String())) + " [" + String(entry.get("type", String())) + "] " + String(entry.get("path", String())));
	}
}

int print_command_response(const Dictionary &p_response, bool p_json_output) {
	if (p_json_output) {
		print_line(JSON::stringify(p_response, "", true));
		return (bool)p_response.get("ok", false) ? 0 : 4;
	}
	if (!(bool)p_response.get("ok", false)) {
		print_line("wgodot: " + String(p_response.get("message", "The editor rejected the request.")));
		return 4;
	}

	const String command = p_response.get("command", String());
	if (command == "run") {
		print_line(vformat("Game started: %s (session %d, PID %d)", String(p_response.get("scene", String())), (int)p_response.get("session", -1), (int64_t)p_response.get("pid", 0)));
	} else if (command == "stop") {
		print_line((bool)p_response.get("was_running", false) ? "Game stopped." : "Game was not running.");
	} else if (command == "tree") {
		print_tree(p_response.get("tree", Array()));
	} else if (command == "ss") {
		const Dictionary screenshot = p_response.get("screenshot", Dictionary());
		print_line(vformat("Screenshot: %s (%dx%d)", String(screenshot.get("path", String())), (int)screenshot.get("width", 0), (int)screenshot.get("height", 0)));
	} else if (command == "observe") {
		const Dictionary screenshot = p_response.get("screenshot", Dictionary());
		print_line(vformat("Screenshot: %s (%dx%d)", String(screenshot.get("path", String())), (int)screenshot.get("width", 0), (int)screenshot.get("height", 0)));
		print_tree(p_response.get("tree", Array()));
	}
	return 0;
}

int run_editor_command(const Dictionary &p_request, bool p_json_output) {
	Dictionary response;
	const int discovery_result = request_editor(p_request, response);
	if (discovery_result != 0) {
		if (p_json_output) {
			print_line(JSON::stringify(response, "", true));
		} else {
			print_line("wgodot: " + String(response.get("message", "No matching editor was found.")));
		}
		return discovery_result;
	}
	return print_command_response(response, p_json_output);
}

int run_game_query_command(const String &p_command, const Vector<String> &p_arguments) {
	GameCommandOptions options;
	const bool allow_tree_options = p_command == "tree" || p_command == "observe";
	const bool allow_output = p_command == "ss" || p_command == "observe";
	if (!parse_game_command_options(p_command, p_arguments, allow_tree_options, allow_output, options)) {
		return 2;
	}

	Dictionary request;
	request["protocol"] = PROTOCOL_VERSION;
	request["command"] = p_command;
	add_game_options_to_request(options, request);
	return run_editor_command(request, options.json_output);
}

int run_run_command(const Vector<String> &p_arguments) {
	bool json_output = false;
	String mode = "main";
	String scene;
	for (const String &argument : p_arguments) {
		if (argument == "--json") {
			json_output = true;
		} else if (argument == "--current") {
			if (mode != "main" || !scene.is_empty()) {
				print_line("wgodot: specify either --current or a scene path, not both.");
				return 2;
			}
			mode = "current";
		} else if (!argument.begins_with("-") && scene.is_empty() && mode == "main") {
			scene = argument;
			mode = "custom";
		} else {
			print_line("wgodot: unknown run argument: " + argument);
			return 2;
		}
	}

	Dictionary options;
	options["mode"] = mode;
	if (!scene.is_empty()) {
		options["scene"] = scene;
	}
	Dictionary request;
	request["protocol"] = PROTOCOL_VERSION;
	request["command"] = "run";
	request["options"] = options;
	return run_editor_command(request, json_output);
}

int run_stop_command(const Vector<String> &p_arguments) {
	bool json_output = false;
	for (const String &argument : p_arguments) {
		if (argument == "--json") {
			json_output = true;
		} else {
			print_line("wgodot: unknown stop argument: " + argument);
			return 2;
		}
	}
	Dictionary request;
	request["protocol"] = PROTOCOL_VERSION;
	request["command"] = "stop";
	return run_editor_command(request, json_output);
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
	if (command == "run") {
		r_exit_code = run_run_command(arguments);
		return true;
	}
	if (command == "stop") {
		r_exit_code = run_stop_command(arguments);
		return true;
	}
	if (command == "tree" || command == "ss" || command == "screenshot" || command == "observe") {
		r_exit_code = run_game_query_command(command == "screenshot" ? "ss" : command, arguments);
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
