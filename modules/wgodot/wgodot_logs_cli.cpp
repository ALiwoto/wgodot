// wgodot-changes::file
/**************************************************************************/
/*  wgodot_logs_cli.cpp                                                   */
/**************************************************************************/

#include "wgodot_logs_cli.h"

#include "wgodot_cli.h"

#include "core/io/json.h"
#include "core/string/print_string.h"

namespace WGodotLogsCLI {

namespace {

struct LogOptions {
	bool json_output = false;
	int session = -1;
	int tail = 100;
	PackedStringArray sources;
	PackedStringArray levels;
};

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
		print_line("wgodot: " + String(p_response.get("message", "The log command failed.")));
	}
}

bool append_values(const String &p_option, const String &p_source, PackedStringArray &r_values, String &r_error) {
	for (String value : p_source.split(",")) {
		value = value.strip_edges().to_lower();
		if (value.is_empty()) {
			r_error = p_option + " requires one or more comma-separated values.";
			return false;
		}
		r_values.push_back(value);
	}
	return true;
}

bool parse_options(const String &p_command, const Vector<String> &p_arguments, LogOptions &r_options, String &r_error) {
	const bool allow_levels = p_command == "logs";
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--json") {
			r_options.json_output = true;
		} else if (argument == "--source") {
			if (i + 1 >= p_arguments.size() || !append_values(argument, p_arguments[++i], r_options.sources, r_error)) {
				if (r_error.is_empty()) {
					r_error = "--source requires output, debugger, or all.";
				}
				return false;
			}
		} else if (allow_levels && argument == "--level") {
			if (i + 1 >= p_arguments.size() || !append_values(argument, p_arguments[++i], r_options.levels, r_error)) {
				if (r_error.is_empty()) {
					r_error = "--level requires standard, info, warning, error, or editor.";
				}
				return false;
			}
		} else if (allow_levels && argument == "--tail") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() <= 0) {
				r_error = "--tail requires a positive integer.";
				return false;
			}
			r_options.tail = p_arguments[++i].to_int();
		} else if (argument == "--session") {
			if (i + 1 >= p_arguments.size() || !p_arguments[i + 1].is_valid_int() || p_arguments[i + 1].to_int() < 0) {
				r_error = "--session requires a non-negative integer session ID.";
				return false;
			}
			r_options.session = p_arguments[++i].to_int();
		} else {
			r_error = "Unknown " + p_command + " argument: " + argument;
			return false;
		}
	}
	return true;
}

void print_output_entries(const Dictionary &p_response) {
	if (!p_response.has("output")) {
		return;
	}
	const Array entries = p_response.get("output", Array());
	print_line(vformat("Output (%d returned, %d matching, %d total):", entries.size(), (int)p_response.get("output_matching", 0), (int)p_response.get("output_total", 0)));
	if (entries.is_empty()) {
		print_line("  (none)");
		return;
	}
	for (const Variant &entry_variant : entries) {
		const Dictionary entry = entry_variant;
		String line = "[" + String(entry.get("level", "standard")) + "] " + String(entry.get("text", String()));
		const int count = entry.get("count", 1);
		if (count > 1) {
			line += vformat(" (x%d)", count);
		}
		print_line(line);
	}
}

void print_debugger_entries(const Dictionary &p_response) {
	if (!p_response.has("debugger")) {
		return;
	}
	if (p_response.has("output")) {
		print_line("");
	}
	const Array entries = p_response.get("debugger", Array());
	print_line(vformat("Debugger errors (%d returned, %d matching, %d total):", entries.size(), (int)p_response.get("debugger_matching", 0), (int)p_response.get("debugger_total", 0)));
	if (entries.is_empty()) {
		print_line("  (none)");
		return;
	}
	for (const Variant &entry_variant : entries) {
		const Dictionary entry = entry_variant;
		print_line(vformat("[%s][session %d][%s] %s", String(entry.get("level", "error")), (int)entry.get("session", -1), String(entry.get("time", String())), String(entry.get("message", String()))));
		const String condition = entry.get("condition", String());
		if (!condition.is_empty()) {
			print_line("  Condition: " + condition);
		}
		const String file = entry.get("file", String());
		const int line = entry.get("line", -1);
		const String function = entry.get("function", String());
		if (!file.is_empty()) {
			String source = "  Source: " + file;
			if (line >= 0) {
				source += ":" + itos(line);
			}
			if (!function.is_empty()) {
				source += " @ " + function;
			}
			print_line(source);
		}
		const Array stack = entry.get("stack", Array());
		if (!stack.is_empty()) {
			print_line("  Stack:");
			for (const Variant &frame_variant : stack) {
				const Dictionary frame = frame_variant;
				print_line(vformat("  - %s:%d @ %s", String(frame.get("file", String())), (int)frame.get("line", -1), String(frame.get("function", String()))));
			}
		}
	}
}

} // namespace

int run(const String &p_command, const Vector<String> &p_arguments) {
	LogOptions options;
	String argument_error;
	if (!parse_options(p_command, p_arguments, options, argument_error)) {
		const Dictionary error = make_error(argument_error);
		print_error(error, options.json_output);
		return 2;
	}

	Dictionary request_options;
	request_options["sources"] = options.sources;
	if (p_command == "logs") {
		request_options["levels"] = options.levels;
		request_options["tail"] = options.tail;
	}
	if (options.session >= 0) {
		request_options["session"] = options.session;
	}
	Dictionary request;
	request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	request["command"] = p_command;
	request["options"] = request_options;
	Dictionary response;
	const int discovery_result = WGodotCLI::request_editor_command(request, response);
	if (discovery_result != 0 || !(bool)response.get("ok", false)) {
		print_error(response, options.json_output);
		return discovery_result != 0 ? discovery_result : 4;
	}

	if (options.json_output) {
		print_line(JSON::stringify(response, "", true));
	} else if (p_command == "clear_logs") {
		print_line(vformat("Cleared %d Output entr%s and %d Debugger error%s.",
				(int)response.get("cleared_output", 0), (int)response.get("cleared_output", 0) == 1 ? "y" : "ies",
				(int)response.get("cleared_debugger", 0), (int)response.get("cleared_debugger", 0) == 1 ? "" : "s"));
	} else {
		print_output_entries(response);
		print_debugger_entries(response);
	}
	return 0;
}

} // namespace WGodotLogsCLI
