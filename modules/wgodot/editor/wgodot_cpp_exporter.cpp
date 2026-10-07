// wgodot-changes::file
#include "wgodot_cpp_exporter.h"

#include "../wgodot_cli.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_project.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"

namespace {

bool valid_output_path(const String &p_path) {
	return p_path.is_absolute_path() && !p_path.begins_with("res://") && !p_path.begins_with("user://");
}

Dictionary failure(const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["message"] = p_message;
	return result;
}

} // namespace

int WGodotCppExporter::run(const Vector<String> &p_arguments) {
	const String usage = "Usage: wg export-cpp <absolute-output-directory> [--target client|server] [--analyze-only] [--trace on|off|true|false] (defaults: client, trace off)";
	if (p_arguments.is_empty()) {
		print_error(usage);
		return 2;
	}
	bool analyze_only = false;
	bool trace_enabled = false;
	String target = "client";
	for (int i = 1; i < p_arguments.size(); i++) {
		if (p_arguments[i] == "--analyze-only") {
			analyze_only = true;
		} else if (p_arguments[i] == "--target") {
			if (++i >= p_arguments.size() || (p_arguments[i] != "client" && p_arguments[i] != "server")) {
				print_error("--target requires client or server.");
				return 2;
			}
			target = p_arguments[i];
		} else if (p_arguments[i] == "--trace") {
			if (++i >= p_arguments.size()) {
				print_error("--trace requires on, off, true, or false.");
				return 2;
			}
			const String value = p_arguments[i].to_lower();
			if (value == "on" || value == "true") {
				trace_enabled = true;
			} else if (value == "off" || value == "false") {
				trace_enabled = false;
			} else {
				print_error("Invalid --trace value: " + p_arguments[i] + ". Expected on, off, true, or false.");
				return 2;
			}
		} else {
			print_error("Unknown C++ export option: " + p_arguments[i]);
			print_error(usage);
			return 2;
		}
	}
	const String output = p_arguments[0];
	if (!valid_output_path(output)) {
		print_error("C++ export requires an absolute filesystem output directory.");
		return 2;
	}
	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		print_error("C++ export requires a project.godot. Supply --path before --wg.");
		return 2;
	}
	Dictionary options;
	options["output"] = output;
	options["analyze_only"] = analyze_only;
	options["trace"] = trace_enabled;
	options["target"] = target;
	Dictionary status_request;
	status_request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	status_request["command"] = "status";
	Dictionary response;
	const int status_result = WGodotCLI::request_editor_command(status_request, response);
	if (status_result == 0 && (bool)response.get("ok", false)) {
		Dictionary request;
		request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
		request["command"] = "export-cpp";
		request["options"] = options;
		const int connection_result = WGodotCLI::request_editor_command(request, response);
		if (connection_result != 0) {
			print_error(response.get("message", "Could not connect to the project editor."));
			return connection_result;
		}
	} else if (status_result != 0 && String(response.get("error", String())) == "editor_not_found") {
		response = execute(options);
	} else {
		print_error(response.get("message", "Could not connect to the project editor."));
		return status_result != 0 ? status_result : 1;
	}
	const PackedStringArray diagnostics = response.get("diagnostics", PackedStringArray());
	for (const String &diagnostic : diagnostics) {
		print_error(diagnostic);
	}
	if (!(bool)response.get("ok", false)) {
		print_error(response.get("message", "C++ export failed."));
		return 1;
	}
	print_line(response["message"]);
	return 0;
}

Dictionary WGodotCppExporter::execute(const Dictionary &p_options) {
	const String output = String(p_options.get("output", String())).replace_char('\\', '/').simplify_path();
	if (!valid_output_path(output)) {
		return failure("C++ export requires an absolute filesystem output directory.");
	}
	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		return failure("C++ export requires a project.godot.");
	}
	PackedStringArray unsaved;
	if (ScriptEditor::get_singleton()) {
		unsaved = ScriptEditor::get_singleton()->get_unsaved_files();
	}
	if (EditorNode::get_singleton()) {
		unsaved.append_array(EditorInterface::get_singleton()->get_unsaved_scenes());
		if (EditorNode::has_unsaved_changes() && unsaved.is_empty()) {
			return failure("Save unsaved editor resources before exporting native code.");
		}
	}
	if (!unsaved.is_empty()) {
		return failure("Save these files before exporting native code:\n" + String("\n").join(unsaved));
	}
	const bool analyze_only = p_options.get("analyze_only", false);
	const bool trace_enabled = p_options.get("trace", false);
	WGodotCppProject project;
	Error result = project.analyze(p_options.get("target", "client"));
	Dictionary report = project.describe();
	Vector<String> diagnostics = project.get_diagnostics();
	if (result == OK && !analyze_only) {
		WGodotCppEmitter emitter(project, trace_enabled);
		result = emitter.generate();
		diagnostics = emitter.get_diagnostics();
		if (result == OK) {
			result = emitter.write(output);
		}
	}
	report["diagnostics"] = diagnostics;
	report["success"] = result == OK;
	report["analysis_only"] = analyze_only;
	report["trace"] = trace_enabled;
	Error write_error = DirAccess::make_dir_recursive_absolute(output);
	if (write_error == OK) {
		Ref<FileAccess> file = FileAccess::open(output.path_join("cpp-export-report.json"), FileAccess::WRITE, &write_error);
		if (file.is_valid()) {
			file->store_string(JSON::stringify(report, "\t", true) + "\n");
			file->flush();
			write_error = file->get_error();
		}
	}
	Dictionary response;
	PackedStringArray messages;
	for (int i = 0; i < MIN(diagnostics.size(), 30); i++) {
		messages.push_back(diagnostics[i]);
	}
	if (diagnostics.size() > 30) {
		messages.push_back(vformat("%d more diagnostics in %s", diagnostics.size() - 30, output.path_join("cpp-export-report.json")));
	}
	response["diagnostics"] = messages;
	response["ok"] = write_error == OK && result == OK;
	if (write_error != OK || result != OK) {
		response["message"] = vformat("C++ export failed: %s", error_names[result != OK ? result : write_error]);
	} else {
		response["message"] = vformat("C++ %s: %d classes. Output: %s", analyze_only ? "analysis complete" : "export complete", project.get_classes().size(), output);
	}
	return response;
}
