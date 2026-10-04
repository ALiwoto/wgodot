// wgodot-changes::file
#include "wgodot_performance_cli.h"

#include "wgodot_cli.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"

namespace WGodotPerformanceCLI {
namespace {

String value_text(double p_value, const String &p_unit) {
	if (p_unit == "bytes") {
		return String::humanize_size(int64_t(p_value));
	}
	if (p_unit == "seconds") {
		return String::num(p_value * 1000.0, 3) + " ms";
	}
	return String::num(p_value, p_unit == "count" ? 0 : 2) + (p_unit == "fps" ? " fps" : "");
}

void print_performance(const Dictionary &p_response) {
	print_line(vformat("Performance (%d sampled frames):", int(p_response.get("frames", 0))));
	const Array monitors = p_response.get("monitors", Array());
	for (const Variant &value : monitors) {
		const Dictionary monitor = value;
		const String unit = monitor["unit"];
		String line = "  " + String(monitor["name"]) + ": " + value_text(monitor["value"], unit);
		if (monitor.has("samples")) {
			const Dictionary stats = monitor["samples"];
			line += " (mean " + value_text(stats["mean"], unit) + ", p95 " + value_text(stats["p95"], unit) + ", max " + value_text(stats["max"], unit) + ")";
		}
		print_line(line);
	}
	const Dictionary intervals = p_response.get("frame_interval_ms", Dictionary());
	if (!intervals.is_empty()) {
		print_line(vformat("  Frame interval: mean %.3f ms, p95 %.3f ms, max %.3f ms", double(intervals["mean"]), double(intervals["p95"]), double(intervals["max"])));
	}
}

void print_textures(const Dictionary &p_response) {
	const Array textures = p_response.get("textures", Array());
	print_line(vformat("Textures: %d shown / %d matching / %d total; %s estimated allocation (%d aliases excluded).", textures.size(), int(p_response.get("matching_textures", 0)), int(p_response.get("total_textures", 0)), String::humanize_size(p_response.get("estimated_allocation_bytes", 0)), int(p_response.get("aliases", 0))));
	if (int(p_response.get("frames", 0)) > 0) {
		print_line(vformat("Capture: %d rendered frames, %d observed textures; %s potential savings from sampled 2D uses.", int(p_response["frames"]), int(p_response["observed_textures"]), String::humanize_size(p_response.get("estimated_candidate_savings_bytes", 0))));
	}
	for (const Variant &value : textures) {
		const Dictionary row = value;
		String label = row.get("path", String());
		if (label.is_empty()) {
			label = row.get("resource_path", String());
		}
		if (label.is_empty()) {
			label = row.get("resource_name", String());
		}
		if (label.is_empty()) {
			label = "<unnamed " + String(row.get("resource_class", "texture")) + ">";
		}
		print_line(vformat("  %s  %dx%d %s, %d mip levels, RID %s  %s", String::humanize_size(row["estimated_bytes"]), int(row["width"]), int(row["height"]), String(row["format"]), int(row["mipmap_levels"]), String(row["rid"]), label));
		if (!String(row["alias_of"]).is_empty()) {
			print_line("    Alias of RID " + String(row["alias_of"]));
		}
		if (bool(row["render_target"])) {
			print_line("    Render target");
		}
		if (row.has("max_rendered_pixels")) {
			const Array size = row["max_rendered_pixels"];
			print_line(vformat("    Largest submitted bounds: %.1fx%.1f pixels; %d frames; %s", double(size[0]), double(size[1]), int(row["frames_seen"]), bool(row["measurement_uncertain"]) ? "mapping uncertain; no resize estimate" : "measured 2D mapping"));
			if (row.has("candidate_size")) {
				const Array candidate = row["candidate_size"];
				print_line(vformat("    Sample-based candidate: %dx%d; save ~%s (includes headroom)", int(candidate[0]), int(candidate[1]), String::humanize_size(row["estimated_savings_bytes"])));
			}
			const Array nodes = row.get("nodes", Array());
			for (const Variant &node : nodes) {
				print_line("    " + String(node));
			}
			if (bool(row.get("nodes_truncated", false))) {
				print_line("    Additional canvas items omitted");
			}
		} else if (String(row["observation"]) == "not_observed_2d") {
			print_line("    Not observed in the captured 2D commands");
		}
	}
}

} // namespace

int run(const String &p_command, const Vector<String> &p_arguments) {
	const bool json_output = p_arguments.has("--json");
	Dictionary options;
	String output;
	String argument_error;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String argument = p_arguments[i];
		if (argument == "--json") {
			continue;
		}
		if (argument == "--unused" && p_command == "textures") {
			options["unused"] = true;
			continue;
		}
		const bool texture_option = p_command == "textures" && (argument == "--limit" || argument == "--filter" || argument == "--sort" || argument == "--headroom");
		if (argument != "--frames" && argument != "--timeout" && argument != "--session" && argument != "--output" && !texture_option) {
			argument_error = "Unknown " + p_command + " argument: " + argument;
			break;
		}
		if (i + 1 == p_arguments.size()) {
			argument_error = "Missing value for " + argument;
			break;
		}
		const String value = p_arguments[++i];
		const String key = argument.trim_prefix("--");
		if (key == "output") {
			output = value;
		} else if (key == "filter" || key == "sort") {
			options[key] = value;
		} else if (key == "headroom") {
			if (!value.is_valid_float()) {
				argument_error = "--headroom requires a number from 1 to 4.";
				break;
			}
			options[key] = value.to_float();
		} else {
			if (!value.is_valid_int() || value.to_int() < 0 || value.to_int() > INT32_MAX) {
				argument_error = argument + " requires an integer between 0 and 2147483647.";
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
		request["command"] = p_command;
		if (options.has("session")) {
			request["session"] = options["session"];
			options.erase("session");
		}
		request["options"] = options;
		result = WGodotCLI::request_editor_command(request, response);
	}
	if (result != 0 || !bool(response.get("ok", false))) {
		print_line(json_output ? JSON::stringify(response, "", true) : "wgodot: " + String(response.get("message", "Performance command failed.")));
		return result != 0 ? result : 4;
	}
	if (!output.is_empty()) {
		Error file_error;
		Ref<FileAccess> file = FileAccess::open(output, FileAccess::WRITE, &file_error);
		if (file.is_null()) {
			response["ok"] = false;
			response["error"] = "output_failed";
			response["message"] = "Cannot write report: " + output + " (" + error_names[file_error] + ")";
			print_line(json_output ? JSON::stringify(response, "", true) : String(response["message"]));
			return 4;
		}
		file->store_string(JSON::stringify(response, "\t", true) + "\n");
		file->flush();
		if (file->get_error() != OK) {
			response["ok"] = false;
			response["error"] = "output_failed";
			response["message"] = "Could not finish writing report: " + output;
			print_line(json_output ? JSON::stringify(response, "", true) : String(response["message"]));
			return 4;
		}
	}
	if (json_output) {
		print_line(JSON::stringify(response, "", true));
	} else {
		if (p_command == "perf") {
			print_performance(response);
		} else {
			print_textures(response);
		}
		if (bool(response.get("timed_out", false))) {
			print_line("Capture reached its time limit; results are partial.");
		}
		if (bool(response.get("capture_truncated", false))) {
			print_line("Capture reached its texture limit; results are partial.");
		}
		print_line(String(response.get("note", String())));
		if (!output.is_empty()) {
			print_line("JSON report: " + output);
		}
	}
	return 0;
}

} // namespace WGodotPerformanceCLI
