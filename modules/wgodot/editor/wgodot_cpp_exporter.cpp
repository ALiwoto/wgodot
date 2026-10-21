// wgodot-changes::file
#include "wgodot_cpp_exporter.h"

#include "../wgodot_cli.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_memory_profile.h"
#include "wgodot_cpp_project.h"
#include "wgodot_cpp_cache.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/wgodot_editor_activity.h"

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

// Resource loaders and engine checks can log an error while returning a usable
// value. Such an export must not be published or reported as successful. The
// handler also observes errors from the export's worker threads.
class ExportErrors {
	ErrorHandlerList handler;
	Mutex mutex;
	Vector<String> messages;
	uint64_t count = 0;

	static void capture(void *p_data, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
		if (p_type == ERR_HANDLER_WARNING) {
			return;
		}
		auto &self = *static_cast<ExportErrors *>(p_data);
		MutexLock lock(self.mutex);
		self.count++;
		if (self.messages.size() < 30) {
			const String message = String::utf8(p_message && *p_message ? p_message : p_error);
			self.messages.push_back(message + " (" + String::utf8(p_file) + ":" + itos(p_line) + ")");
		}
	}

public:
	ExportErrors() {
		handler.errfunc = capture;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~ExportErrors() {
		remove_error_handler(&handler);
	}

	bool any() const {
		MutexLock lock(mutex);
		return count != 0;
	}

	Vector<String> diagnostics() const {
		MutexLock lock(mutex);
		Vector<String> result = messages;
		if (count > uint64_t(messages.size())) {
			result.push_back(uitos(count - messages.size()) + " additional engine errors; see the export error log.");
		}
		return result;
	}

	Dictionary failure_response(const String &p_message) const {
		Dictionary response = failure(p_message);
		response["diagnostics"] = diagnostics();
		return response;
	}
};

} // namespace

int WGodotCppExporter::run(const Vector<String> &p_arguments) {
	const String usage = "Usage: wg export-cpp <absolute-output-directory> [--target client|server] [--jobs N] [--analyze-only] [--profile-memory] [--standalone] [--trace on|off|true|false] (defaults: client, jobs 0 = available worker threads, trace off). --standalone exports saved files in this process without using the editor. --profile-memory writes allocation/resource CSV diagnostics to the output directory.";
	if (p_arguments.is_empty()) {
		print_error(usage);
		return 2;
	}
	bool analyze_only = false;
	bool profile_memory = false;
	bool standalone = false;
	bool trace_enabled = false;
	int jobs = 0;
	String target = "client";
	for (int i = 1; i < p_arguments.size(); i++) {
		if (p_arguments[i] == "--analyze-only") {
			analyze_only = true;
		} else if (p_arguments[i] == "--profile-memory") {
			profile_memory = true;
		} else if (p_arguments[i] == "--standalone") {
			standalone = true;
		} else if (p_arguments[i] == "--jobs") {
			if (++i >= p_arguments.size() || !p_arguments[i].is_valid_int() || p_arguments[i].to_int() < 0 || p_arguments[i].to_int() > INT32_MAX) {
				print_error("--jobs requires a nonnegative integer (0 = available worker threads).");
				return 2;
			}
			jobs = p_arguments[i].to_int();
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
	options["profile_memory"] = profile_memory;
	options["trace"] = trace_enabled;
	options["target"] = target;
	options["jobs"] = jobs;
	Dictionary response;
	if (standalone) {
		response = execute(options);
	} else {
		Dictionary status_request;
		status_request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
		status_request["command"] = "status";
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
	const Variant requested_jobs = p_options.get("jobs", 0);
	if (requested_jobs.get_type() != Variant::INT || int64_t(requested_jobs) < 0 || int64_t(requested_jobs) > INT32_MAX) {
		return failure("jobs must be a nonnegative integer (0 = available worker threads).");
	}
	const int available_jobs = WorkerThreadPool::get_singleton()->get_thread_count();
	const int jobs = int64_t(requested_jobs) == 0 ? available_jobs : MIN(int64_t(requested_jobs), available_jobs);
	ExportErrors engine_errors;
	WGodotCppCache cache;
	if (!analyze_only) {
		print_line("Checking native generation cache...");
		const Error cache_error = cache.open(output, p_options.get("target", "client"), trace_enabled);
		if (cache_error != OK) {
			return engine_errors.failure_response("Cannot inspect native generation inputs: " + String(error_names[cache_error]));
		}
		const bool up_to_date = cache.up_to_date();
		if (engine_errors.any()) {
			return engine_errors.failure_response("Engine errors while inspecting native generation inputs.");
		}
		if (up_to_date) {
			Dictionary response;
			response["ok"] = true;
			response["message"] = vformat("C++ export up to date: %d classes; analysis and generation skipped. Output: %s", cache.class_count(), output);
			return response;
		}
	}
	WGodotEditorActivity activity("Exporting native C++", output);
	const bool profile_memory = p_options.get("profile_memory", false);
	WGodotCppMemoryProfile memory_profile(profile_memory ? output : String());
	if (profile_memory && !memory_profile.is_open()) {
		return failure("Cannot create memory diagnostics in: " + output);
	}
	WGodotCppProject project;
	uint64_t started = OS::get_singleton()->get_ticks_msec();
	print_line("Analyzing native scripts...");
	Error result = project.analyze(p_options.get("target", "client"), jobs);
	if (result == OK && engine_errors.any()) {
		result = ERR_COMPILATION_FAILED;
	}
	WGodotCppMemoryProfile::sample("analysis-complete");
	WGodotCppMemoryProfile::snapshot_resources("analysis-complete");
	print_line(vformat("Native analysis: %d ms", OS::get_singleton()->get_ticks_msec() - started));
	Dictionary report = project.describe();
	Vector<String> diagnostics = project.get_diagnostics();
	bool output_started = false;
	if (result == OK && !analyze_only) {
		project.cache_dependencies(cache);
		if (engine_errors.any()) {
			return engine_errors.failure_response("Engine errors while collecting native script dependencies.");
		}
		WGodotCppEmitter emitter(project, trace_enabled);
		started = OS::get_singleton()->get_ticks_msec();
		print_line(vformat("Generating native scripts and authored scene/resource factories (up to %d workers)...", jobs));
		result = emitter.generate(output, jobs, &cache);
		if (result == OK && engine_errors.any()) {
			result = ERR_COMPILATION_FAILED;
		}
		WGodotCppMemoryProfile::sample("generation-complete");
		print_line(vformat("Native generation: %d ms", OS::get_singleton()->get_ticks_msec() - started));
		diagnostics = emitter.get_diagnostics();
		if (result == OK) {
			started = OS::get_singleton()->get_ticks_msec();
			print_line("Writing native output...");
			output_started = true;
			result = emitter.write(output);
			WGodotCppMemoryProfile::sample("output-complete");
			print_line(vformat("Native output: %d ms", OS::get_singleton()->get_ticks_msec() - started));
		}
	}
	WGodotCppMemoryProfile::sample("emitter-released");
	if (engine_errors.any()) {
		diagnostics.append_array(engine_errors.diagnostics());
		if (result == OK) {
			result = ERR_COMPILATION_FAILED;
		}
		if (output_started) {
			const String manifest = output.path_join("main_game.json");
			if (FileAccess::exists(manifest)) {
				DirAccess::remove_absolute(manifest);
			}
		}
	}
	report["diagnostics"] = diagnostics;
	report["success"] = result == OK;
	report["analysis_only"] = analyze_only;
	report["trace"] = trace_enabled;
	report["jobs_requested"] = requested_jobs;
	report["jobs_effective"] = jobs;
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
