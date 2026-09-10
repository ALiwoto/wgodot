// wgodot-changes::file

#include "export_pipeline.h"

#include "../wgodot_stdlib.h"
#include "export_pass_ast.h"
#include "export_pass_dead_code.h"
#include "export_pass_diagnostics.h"
#include "export_pass_no_export.h"
#include "export_timing.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_file_system.h"
#endif

namespace WGodotGDScriptExportTransform {

namespace {
#ifdef TOOLS_ENABLED
void collect_project_scripts(EditorFileSystemDirectory *p_directory, HashSet<String> &r_paths) {
	for (int i = 0; i < p_directory->get_file_count(); i++) {
		const String path = p_directory->get_file_path(i);
		if (path.get_extension() == "gd") {
			r_paths.insert(path);
		}
	}
	for (int i = 0; i < p_directory->get_subdir_count(); i++) {
		collect_project_scripts(p_directory->get_subdir(i), r_paths);
	}
}
#endif
} // namespace

void ExportPipeline::reset() {
	ready = false;
	original = ExportProject();
	current = ExportProject();
	artifacts.reset();
}

Error ExportPipeline::prepare(const HashSet<String> &p_paths, const TransformOptions &p_options, String &r_error) {
	reset();
	artifacts.set_options(p_options);
	HashSet<String> scripts;
	for (const String &path : p_paths) {
		if (path.get_extension() == "gd") {
			scripts.insert(path);
		}
	}
#ifdef TOOLS_ENABLED
	if (EditorFileSystem::get_singleton() != nullptr) {
		collect_project_scripts(EditorFileSystem::get_singleton()->get_filesystem(), scripts);
	}
#endif
	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	for (const StringName &name : global_classes) {
		const String path = ScriptServer::get_global_class_path(name);
		if (path.get_extension() == "gd") {
			scripts.insert(path);
		}
	}
	for (int i = 0; i < WGodotGDScriptStdLib::get_builtin_interface_count(); i++) {
		scripts.insert(WGodotGDScriptStdLib::get_builtin_interface_path(i));
	}
	Error error = original.capture(p_paths, scripts, r_error);
	if (error != OK) {
		return error;
	}

	NoExportPass no_export;
	DiagnosticPass diagnostics;
	DeadCodePass dead_code;
	ConstantsPass constants;
	NamesPass names;
	BuiltinAliasesPass builtin_aliases;
	PathsPass paths;
	StringsPass strings;
	CleanupPass cleanup;
	ExportTransformationBase *configured_passes[] = {
		&no_export,
		&dead_code,
		&diagnostics,
		&constants,
		&builtin_aliases,
		&names,
		&paths,
		&strings,
		&cleanup,
	};
	Vector<ExportTransformationBase *> passes;
	for (ExportTransformationBase *pass : configured_passes) {
		if (pass->is_enabled(p_options)) {
			passes.push_back(pass);
		}
	}
	HashMap<StringName, int> positions;
	for (int i = 0; i < passes.size(); i++) {
		const StringName name(passes[i]->get_name());
		if (positions.has(name)) {
			r_error = "Duplicate export pass: " + String(name);
			return ERR_INVALID_PARAMETER;
		}
		positions[name] = i;
	}
	for (int i = 0; i < passes.size(); i++) {
		for (const StringName &predecessor : passes[i]->get_predecessors()) {
			const int *position = positions.getptr(predecessor);
			if (position != nullptr && *position >= i) {
				r_error = vformat("Export pass '%s' must run after '%s'.", passes[i]->get_name(), predecessor);
				return ERR_INVALID_PARAMETER;
			}
		}
	}
	{
		ExportAnalysis analysis(original, artifacts);
		ExportAnalysis::Scope scope(analysis);
		const ExportAnalysisInput input{ { original, artifacts }, analysis };
		for (ExportTransformationBase *pass : passes) {
			export_timing_log_checkpoint(p_options, pass->get_name(), "prescan");
			error = pass->prescan(input, r_error);
			if (error != OK) {
				r_error = String(pass->get_name()) + " prescan: " + r_error;
				return error;
			}
		}
	}

	current = original;
	for (ExportTransformationBase *pass : passes) {
		const uint64_t started = export_timing_get_ticks_usec();
		ExportPassOutput output(artifacts);
		ExportProject next;
		{
			ExportAnalysis analysis(current, artifacts);
			ExportAnalysis::Scope scope(analysis);
			const ExportAnalysisInput input{ { current, artifacts }, analysis };
			export_timing_log_checkpoint(p_options, pass->get_name(), "analysis");
			error = pass->analyze(input, r_error);
			if (error == OK) {
				export_timing_log_checkpoint(p_options, pass->get_name(), "transformation");
				error = pass->transform(input, output, r_error);
			}
			if (error == OK) {
				error = current.apply(output.edits, pass->get_name(), next, r_error);
			}
		}
		if (error != OK) {
			r_error = String(pass->get_name()) + ": " + r_error;
			return error;
		}
		current = next;
		artifacts = output.artifacts;
		export_timing_log_slow_phase(p_options, String(), pass->get_name(), export_timing_get_ticks_usec() - started);
	}
	{
		ExportAnalysis analysis(current, artifacts);
		error = analysis.analyze_scripts(true, r_error);
		if (error != OK) {
			r_error = "Final export validation: " + r_error;
			return error;
		}
	}
	ready = true;
	return OK;
}

} // namespace WGodotGDScriptExportTransform
