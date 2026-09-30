// wgodot-changes::file
#include "wgodot_cpp_exporter.h"

#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_project.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"

int WGodotCppExporter::run(const Vector<String> &p_arguments) {
	const String usage = "Usage: wg export-cpp <absolute-output-directory> [--analyze-only] [--trace on|off|true|false] (default: off)";
	if (p_arguments.is_empty()) {
		print_error(usage);
		return 2;
	}
	bool analyze_only = false;
	bool trace_enabled = false;
	for (int i = 1; i < p_arguments.size(); i++) {
		if (p_arguments[i] == "--analyze-only") {
			analyze_only = true;
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
	const String output = p_arguments[0].replace_char('\\', '/').simplify_path();
	if (!output.is_absolute_path() || output.begins_with("res://") || output.begins_with("user://")) {
		print_error("C++ export requires an absolute filesystem output directory.");
		return 2;
	}
	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		print_error("C++ export requires a project.godot. Supply --path before --wg.");
		return 2;
	}
	WGodotCppProject project;
	Error result = project.analyze();
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
	for (int i = 0; i < MIN(diagnostics.size(), 30); i++) {
		print_error(diagnostics[i]);
	}
	if (diagnostics.size() > 30) {
		print_error(vformat("%d more diagnostics in %s", diagnostics.size() - 30, output.path_join("cpp-export-report.json")));
	}
	if (write_error != OK || result != OK) {
		print_error(vformat("C++ export failed: %s", error_names[result != OK ? result : write_error]));
		return 1;
	}
	print_line(vformat("C++ %s: %d classes. Output: %s", analyze_only ? "analysis complete" : "export complete", project.get_classes().size(), output));
	return 0;
}
