// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_cli.cpp                                                  */
/**************************************************************************/

#include "wgodot_debug_cli.h"

#include "wgodot_cli.h"

#include "core/io/json.h"
#include "core/string/print_string.h"

namespace WGodotDebugCLI {

namespace {

Dictionary make_error(const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = "invalid_arguments";
	response["message"] = p_message;
	return response;
}

void print_error(const Dictionary &p_response, bool p_json_output) {
	if (p_json_output) {
		print_line(JSON::stringify(p_response, "", true));
	} else {
		print_line("wgodot: " + String(p_response.get("message", "The debug command failed.")));
	}
}

int send_request(const String &p_command, const Dictionary &p_options, int p_session, bool p_json_output, Dictionary &r_response) {
	Dictionary request;
	request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	request["command"] = p_command;
	request["options"] = p_options;
	if (p_session >= 0) {
		request["session"] = p_session;
	}
	const int discovery_result = WGodotCLI::request_editor_command(request, r_response);
	if (discovery_result != 0 || !(bool)r_response.get("ok", false)) {
		print_error(r_response, p_json_output);
		return discovery_result != 0 ? discovery_result : 4;
	}
	return 0;
}

bool parse_breakpoint_location(const String &p_location, String &r_path, int &r_line) {
	const int separator = p_location.rfind(":");
	if (separator < 0) {
		return false;
	}
	const String line_text = p_location.substr(separator + 1);
	if (!line_text.is_valid_int() || line_text.to_int() <= 0) {
		return false;
	}
	r_path = p_location.left(separator);
	r_line = line_text.to_int();
	return !r_path.is_empty();
}

void print_breakpoint(const Dictionary &p_breakpoint) {
	print_line(vformat("#%d %s:%d [%s]", (int)p_breakpoint.get("id", 0), String(p_breakpoint.get("path", String())), (int)p_breakpoint.get("line", 0), (bool)p_breakpoint.get("enabled", false) ? "enabled" : "disabled"));
}

void print_debug_state(const Dictionary &p_response) {
	const String state = p_response.get("state", "not_running");
	const int session = p_response.get("session", -1);
	if (state == "not_running") {
		print_line("Debugger: game not running");
		return;
	}
	print_line(vformat("Debugger: %s (session %d)", state, session));
	const String reason = p_response.get("reason", String());
	if (!reason.is_empty()) {
		print_line("Reason: " + reason);
	}
	const Dictionary frame = p_response.get("frame", Dictionary());
	if (!frame.is_empty()) {
		print_line(vformat("Frame 0: %s:%d @ %s", String(frame.get("file", String())), (int)frame.get("line", 0), String(frame.get("function", String()))));
	}
}

} // namespace

int run_breakpoint(const Vector<String> &p_arguments) {
	bool json_output = false;
	Vector<String> operands;
	for (const String &argument : p_arguments) {
		if (argument == "--json") {
			json_output = true;
		} else {
			operands.push_back(argument);
		}
	}
	if (operands.is_empty()) {
		const Dictionary error = make_error("breakpoint requires add, list, remove, enable, disable, or clear.");
		print_error(error, json_output);
		return 2;
	}

	const String action = operands[0].to_lower();
	Dictionary options;
	options["action"] = action;
	if (action == "add") {
		if (operands.size() != 2) {
			const Dictionary error = make_error("breakpoint add requires <script-path>:<line>.");
			print_error(error, json_output);
			return 2;
		}
		String path;
		int line = 0;
		if (!parse_breakpoint_location(operands[1], path, line)) {
			const Dictionary error = make_error("Breakpoint location must use <script-path>:<positive-line>.");
			print_error(error, json_output);
			return 2;
		}
		options["path"] = path;
		options["line"] = line;
	} else if (action == "remove" || action == "enable" || action == "disable") {
		if (operands.size() != 2 || !operands[1].is_valid_int() || operands[1].to_int() <= 0) {
			const Dictionary error = make_error("breakpoint " + action + " requires a positive breakpoint ID.");
			print_error(error, json_output);
			return 2;
		}
		options["id"] = operands[1].to_int();
	} else if (action == "list" || action == "clear") {
		if (operands.size() != 1) {
			const Dictionary error = make_error("breakpoint " + action + " does not accept operands.");
			print_error(error, json_output);
			return 2;
		}
	} else {
		const Dictionary error = make_error("Unknown breakpoint action: " + action);
		print_error(error, json_output);
		return 2;
	}

	Dictionary response;
	const int result = send_request("breakpoint", options, -1, json_output, response);
	if (result != 0) {
		return result;
	}
	if (json_output) {
		print_line(JSON::stringify(response, "", true));
	} else if (action == "list") {
		const Array breakpoints = response.get("breakpoints", Array());
		if (breakpoints.is_empty()) {
			print_line("No breakpoints.");
		}
		for (const Variant &breakpoint_variant : breakpoints) {
			const Dictionary breakpoint = breakpoint_variant;
			print_breakpoint(breakpoint);
		}
	} else if (action == "clear") {
		print_line(vformat("Cleared %d breakpoint(s).", (int)response.get("cleared", 0)));
	} else {
		const Dictionary breakpoint = response.get("breakpoint", Dictionary());
		print_breakpoint(breakpoint);
	}
	return 0;
}

int run_debug(const Vector<String> &p_arguments) {
	bool json_output = false;
	int session = -1;
	int timeout_seconds = 15;
	Vector<String> operands;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--json") {
			json_output = true;
		} else if (argument == "--session") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() < 0) {
				const Dictionary error = make_error("--session requires a non-negative integer session ID.");
				print_error(error, json_output);
				return 2;
			}
			session = p_arguments[++i].to_int();
		} else if (argument == "--timeout") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() <= 0 || p_arguments[i + 1].to_int() > 60) {
				const Dictionary error = make_error("--timeout requires a number of seconds from 1 to 60.");
				print_error(error, json_output);
				return 2;
			}
			timeout_seconds = p_arguments[++i].to_int();
		} else {
			operands.push_back(argument);
		}
	}
	if (operands.size() != 1) {
		const Dictionary error = make_error("debug requires state, pause, continue, step_into, step_over, step_out, or wait.");
		print_error(error, json_output);
		return 2;
	}
	const String action = operands[0].to_lower();
	if (action != "state" && action != "pause" && action != "continue" && action != "step_into" && action != "step_over" && action != "step_out" && action != "wait") {
		const Dictionary error = make_error("Unknown debug action: " + action);
		print_error(error, json_output);
		return 2;
	}

	Dictionary options;
	options["action"] = action;
	options["timeout_msec"] = timeout_seconds * 1000;
	Dictionary response;
	const int result = send_request("debug", options, session, json_output, response);
	if (result != 0) {
		return result;
	}
	if (json_output) {
		print_line(JSON::stringify(response, "", true));
	} else {
		print_debug_state(response);
	}
	return 0;
}

} // namespace WGodotDebugCLI
