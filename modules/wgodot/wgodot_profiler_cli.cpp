// wgodot-changes::file
#include "wgodot_profiler_cli.h"

#include "wgodot_cli.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"

namespace WGodotProfilerCLI {
namespace {

void print_report(const Dictionary &p_response) {
	print_line(vformat("Profiler: %s; %d received frames, %d retained records; function cap %d; native calls %s.", bool(p_response.get("profiling", false)) ? "running" : "stopped", int64_t(p_response.get("frames_received", 0)), int(p_response.get("retained_records", 0)), int(p_response.get("max_functions", 0)), bool(p_response.get("native_calls", false)) ? "enabled" : "disabled"));
	if (bool(p_response.get("awaiting_total", false))) {
		print_line("Waiting for accumulated totals from the game.");
	}
	const String view = p_response.get("view", String());
	if (view == "frames") {
		print_line("  Frame       Frame ms     Process ms     Physics ms");
		const Array frames = p_response["frames"];
		for (const Variant &value : frames) {
			const Dictionary frame = value;
			print_line(vformat("  %8d    %10.3f     %10.3f     %10.3f", int(frame["frame"]), double(frame["frame_ms"]), double(frame["process_ms"]), double(frame["physics_ms"])));
		}
	} else if (!view.is_empty()) {
		print_line(vformat("%s (frame %d): %d matching / %d available functions; sorted by %s.", view == "accumulated" ? "Accumulated capture" : "Single frame", int(p_response["frame"]), int(p_response["matching_functions"]), int(p_response["available_functions"]), String(p_response["sort"])));
		print_line("  Self ms     Inclusive ms   Native ms       Calls   Avg self ms   Function");
		const Array functions = p_response["functions"];
		for (const Variant &value : functions) {
			const Dictionary row = value;
			print_line(vformat("  %10.3f  %12.3f  %10.3f  %10d  %12.6f   %s", double(row["self_ms"]), double(row["inclusive_ms"]), double(row["native_ms"]), int(row["calls"]), double(row["average_self_ms"]), String(row["signature"])));
		}
	}
	if (bool(p_response.get("possibly_truncated", false))) {
		print_line("Function cap reached: other functions may be absent from this record.");
	}
	if (bool(p_response.get("partial", false))) {
		print_line("Capture stopped before the requested number of frames arrived.");
	}
	if (bool(p_response.get("timed_out", false))) {
		print_line("Capture reached its time limit.");
	}
	const String note = p_response.get("note", String());
	if (!note.is_empty()) {
		print_line(note);
	}
}

} // namespace

int run(const Vector<String> &p_arguments) {
	const bool json_output = p_arguments.has("--json");
	Dictionary options;
	String output;
	String argument_error;
	int first_option = 0;
	if (!p_arguments.is_empty() && !p_arguments[0].begins_with("--")) {
		options["action"] = p_arguments[0];
		first_option = 1;
	}
	const String action = options.get("action", "report");
	for (int i = first_option; i < p_arguments.size(); i++) {
		const String argument = p_arguments[i];
		if (argument == "--json") {
			continue;
		}
		const bool start_option = action == "start" || action == "capture";
		const bool report_option = action == "report" || action == "capture" || action == "stop";
		if (argument == "--native" && start_option) {
			options["native"] = true;
			continue;
		}
		const bool allowed = argument == "--session" || argument == "--output" ||
				(argument == "--max-functions" && start_option) ||
				(argument == "--frames" && action == "capture") ||
				(argument == "--timeout" && (action == "capture" || action == "stop")) ||
				((argument == "--limit" || argument == "--sort") && (report_option || action == "frames")) ||
				(argument == "--filter" && report_option) ||
				((argument == "--view" || argument == "--frame") && action == "report");
		if (!allowed || i + 1 >= p_arguments.size()) {
			argument_error = "Unknown or incomplete profile " + action + " argument: " + argument;
			break;
		}
		const String value = p_arguments[++i];
		const String key = argument.trim_prefix("--");
		if (key == "output") {
			output = value;
		} else if (key == "filter" || key == "sort" || key == "view") {
			options[key] = value;
		} else {
			if (!value.is_valid_int() || value.to_int() < 0 || value.to_int() > INT32_MAX) {
				argument_error = argument + " requires a nonnegative integer up to 2147483647.";
				break;
			}
			options[key] = value.to_int();
		}
	}
	Dictionary response;
	int result = 0;
	if (!argument_error.is_empty()) {
		response["ok"] = false;
		response["error"] = "invalid_arguments";
		response["message"] = argument_error;
		result = 2;
	} else {
		Dictionary request;
		request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
		request["command"] = "profile";
		if (options.has("session")) {
			request["session"] = options["session"];
			options.erase("session");
		}
		request["options"] = options;
		result = WGodotCLI::request_editor_command(request, response);
	}
	if (result != 0 || !bool(response.get("ok", false))) {
		print_line(json_output ? JSON::stringify(response, "", true) : "wgodot: " + String(response.get("message", "Profiler command failed.")));
		return result != 0 ? result : 4;
	}
	if (!output.is_empty()) {
		Error file_error;
		Ref<FileAccess> file = FileAccess::open(output, FileAccess::WRITE, &file_error);
		if (file.is_valid()) {
			file->store_string(JSON::stringify(response, "\t", true) + "\n");
			file->flush();
			file_error = file->get_error();
		}
		if (file_error != OK) {
			response["ok"] = false;
			response["error"] = "output_failed";
			response["message"] = "Cannot write profiler report: " + output + " (" + error_names[file_error] + ")";
			print_line(json_output ? JSON::stringify(response, "", true) : String(response["message"]));
			return 4;
		}
	}
	if (json_output) {
		print_line(JSON::stringify(response, "", true));
	} else {
		print_report(response);
		if (!output.is_empty()) {
			print_line("JSON report: " + output);
		}
	}
	return 0;
}

} // namespace WGodotProfilerCLI
