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

String frame_text(const Dictionary &p_frame) {
	return vformat("Frame #%d: %s:%d @ %s", (int)p_frame.get("index", 0), String(p_frame.get("file", String())), (int)p_frame.get("line", 0), String(p_frame.get("function", String())));
}

void print_selected_frame(const Dictionary &p_response) {
	const Dictionary frame = p_response.get("selected_frame", Dictionary());
	if (frame.is_empty()) {
		print_line("No stack frame is selected.");
		return;
	}
	print_line(frame_text(frame));
}

void print_stack(const Dictionary &p_response) {
	const Array frames = p_response.get("frames", Array());
	if (frames.is_empty()) {
		print_line("No stack frames.");
		return;
	}
	const int selected_frame = p_response.get("selected_frame_index", 0);
	print_line("Stack:");
	for (const Variant &frame_variant : frames) {
		const Dictionary frame = frame_variant;
		const int index = frame.get("index", 0);
		print_line(vformat("%s #%d %s:%d @ %s", index == selected_frame ? "*" : " ", index, String(frame.get("file", String())), (int)frame.get("line", 0), String(frame.get("function", String()))));
	}
}

void print_scope(const String &p_title, const Array &p_variables) {
	print_line(p_title + ":");
	if (p_variables.is_empty()) {
		print_line("- <none>");
		return;
	}
	for (const Variant &variable_variant : p_variables) {
		const Dictionary variable = variable_variant;
		print_line(vformat("- %s: %s = %s", String(variable.get("name", String())), String(variable.get("type", "Variant")), String(variable.get("value", String()))));
	}
}

void print_frame_variables(const Dictionary &p_response) {
	print_selected_frame(p_response);
	const String action = p_response.get("action", String());
	const Dictionary target = p_response.get("target", Dictionary());
	if (!target.is_empty()) {
		print_line(vformat("Target: %s [%s] %s", String(target.get("path", String())), String(target.get("type", "Object")), String(target.get("value", String()))));
	}
	if (action == "locals" || action == "vars") {
		print_scope("Locals", p_response.get("locals", Array()));
	}
	if (action == "members" || action == "vars") {
		print_scope("Members", p_response.get("members", Array()));
	}
	if (action == "globals" || action == "vars") {
		print_scope("Globals", p_response.get("globals", Array()));
	}
}

void print_debug_state(const Dictionary &p_response) {
	const String action = p_response.get("action", String());
	if (action == "stack") {
		print_stack(p_response);
		return;
	}
	if (action == "frame" || action == "locals" || action == "members" || action == "globals" || action == "vars") {
		print_frame_variables(p_response);
		return;
	}

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
	const Dictionary frame = p_response.get("selected_frame", Dictionary());
	if (!frame.is_empty()) {
		print_line(frame_text(frame));
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
	bool all_members = false;
	bool exclude_builtin = false;
	int session = -1;
	int frame = -1;
	int timeout_seconds = 15;
	String member_target;
	Vector<String> operands;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--json") {
			json_output = true;
		} else if (argument == "--all") {
			all_members = true;
		} else if (argument == "--exclude-builtin") {
			exclude_builtin = true;
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
		} else if (argument == "--frame") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() < 0) {
				const Dictionary error = make_error("--frame requires a non-negative stack frame index.");
				print_error(error, json_output);
				return 2;
			}
			frame = p_arguments[++i].to_int();
		} else {
			operands.push_back(argument);
		}
	}
	if (operands.is_empty()) {
		const Dictionary error = make_error("debug requires state, pause, continue, step_into, step_over, step_out, wait, stack, frame, locals, members, globals, or vars.");
		print_error(error, json_output);
		return 2;
	}
	String action = operands[0].to_lower();
	if (action == "resume") {
		action = "continue";
	}
	const bool scope_action = action == "locals" || action == "members" || action == "globals" || action == "vars";
	const bool simple_action = action == "state" || action == "pause" || action == "continue" || action == "step_into" || action == "step_over" || action == "step_out" || action == "wait" || action == "stack";
	if (!simple_action && !scope_action && action != "frame") {
		const Dictionary error = make_error("Unknown debug action: " + action);
		print_error(error, json_output);
		return 2;
	}
	if (action == "frame") {
		if (frame >= 0) {
			const Dictionary error = make_error("Use debug frame <index>; --frame is only for scoped variable commands.");
			print_error(error, json_output);
			return 2;
		}
		if (operands.size() > 2 || (operands.size() == 2 && (!operands[1].is_valid_int() || operands[1].to_int() < 0))) {
			const Dictionary error = make_error("debug frame accepts an optional non-negative frame index.");
			print_error(error, json_output);
			return 2;
		}
		if (operands.size() == 2) {
			frame = operands[1].to_int();
		}
	} else if (action == "members") {
		if (operands.size() > 2) {
			const Dictionary error = make_error("debug members accepts at most one nested member path.");
			print_error(error, json_output);
			return 2;
		}
		if (operands.size() == 2) {
			member_target = operands[1];
		}
	} else if (operands.size() != 1) {
		const Dictionary error = make_error("debug " + action + " does not accept operands.");
		print_error(error, json_output);
		return 2;
	}
	if (frame >= 0 && !scope_action && action != "frame") {
		const Dictionary error = make_error("--frame is only valid with debug locals, members, globals, or vars.");
		print_error(error, json_output);
		return 2;
	}
	if ((all_members || exclude_builtin) && action != "members") {
		const Dictionary error = make_error("--all and --exclude-builtin are only valid with debug members.");
		print_error(error, json_output);
		return 2;
	}

	Dictionary options;
	options["action"] = action;
	options["timeout_msec"] = timeout_seconds * 1000;
	if (frame >= 0) {
		options["frame"] = frame;
	}
	options["all"] = all_members;
	options["exclude_builtin"] = exclude_builtin;
	if (!member_target.is_empty()) {
		options["target"] = member_target;
	}
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
