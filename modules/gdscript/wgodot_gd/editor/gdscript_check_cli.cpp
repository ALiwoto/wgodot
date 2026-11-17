// wgodot-changes::file
/**************************************************************************/
/*  gdscript_check_cli.cpp                                                */
/**************************************************************************/

#include "gdscript_check_cli.h"
#include "modules/wgodot/editor/wgodot_check_diagnostics.h"

#include "core/error/error_list.h"
#include "editor/file_system/editor_file_system.h"
#include "core/io/file_access.h"
#include "core/object/editor_language.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_linter.h"
#include "export/export_analysis.h"
#include "export/export_context.h"

namespace WGodotGDScriptCheckCLI {
using namespace WGodotGDScriptExportTransform;

namespace {

struct CheckStats {
	int script_count = 0;
	int error_count = 0;
	int warning_count = 0;
	int directory_error_count = 0;
	int read_error_count = 0;
	Array diagnostics;
};

void append_diagnostic(CheckStats &r_stats, const String &p_path, int p_line, const String &p_message, bool p_warning = false) {
	Dictionary diagnostic;
	diagnostic["path"] = p_path;
	diagnostic["line"] = p_line;
	diagnostic["message"] = p_message;
	diagnostic["warning"] = p_warning;
	r_stats.diagnostics.push_back(diagnostic);
}

void collect_script_paths(EditorFileSystemDirectory *p_directory, Vector<String> &r_paths) {
	for (int index = 0; index < p_directory->get_file_count(); index++) {
		if (p_directory->get_file(index).ends_with(".gd")) {
			r_paths.push_back(p_directory->get_file_path(index));
		}
	}
	for (int index = 0; index < p_directory->get_subdir_count(); index++) {
		collect_script_paths(p_directory->get_subdir(index), r_paths);
	}
}

void append_script_error(const EditorLanguage::ScriptError &p_error, const String &p_fallback_path, CheckStats &r_stats, PackedStringArray &r_output) {
	const String path = p_error.path.is_empty() ? p_fallback_path : p_error.path;
	const int line = p_error.start_line > 0 ? p_error.start_line : 0;
	const int column = p_error.start_column > 0 ? p_error.start_column : 0;
	r_output.push_back(vformat("%s:%d:%d: error: %s", path, line, column, p_error.message));
	append_diagnostic(r_stats, path, line, p_error.message);
}

void append_script_warning(const EditorLanguage::Warning &p_warning, const String &p_path, CheckStats &r_stats, PackedStringArray &r_output) {
	const int line = p_warning.start_line > 0 ? p_warning.start_line : 0;
	r_output.push_back(vformat("%s:%d: warning (%s): %s", p_path, line, p_warning.string_code, p_warning.message));
	append_diagnostic(r_stats, p_path, line, vformat("(%s): %s", p_warning.string_code, p_warning.message), true);
}

struct ScriptCheck {
	Ref<GDScriptParserRef> reference;
	Error error = OK;
	int accepted_errors = 0;
	bool checked = false;
	WGodotCheckDiagnostics native;

	void claim_errors(WGodotCheckBudget &p_budget) {
		const int count = reference->get_parser()->get_errors().size();
		while (accepted_errors < count && p_budget.claim_error()) {
			accepted_errors++;
		}
	}

	void parse(WGodotCheckBudget &p_budget, const SafeFlag &p_cancelled) {
		if (p_cancelled.is_set() || p_budget.exhausted()) {
			return;
		}
		checked = true;
		native.budget = &p_budget;
		WGodotErrorCapture capture(&WGodotCheckDiagnostics::capture, &native);
		error = reference->raise_status(GDScriptParserRef::PARSED);
		claim_errors(p_budget);
	}

	void lint(WGodotCheckBudget &p_budget) {
		if (checked && reference.is_valid() && error == OK && !p_budget.exhausted()) {
			WGodotErrorCapture capture(&WGodotCheckDiagnostics::capture, &native);
			GDScriptLinter linter(*reference->get_parser());
			error = linter.lint();
			claim_errors(p_budget);
		}
	}
};

void append_script_diagnostics(const ScriptCheck &p_check, CheckStats &r_stats, PackedStringArray &r_output) {
	if (!p_check.checked || p_check.reference.is_null()) {
		return;
	}
	r_stats.error_count += p_check.native.errors;
	r_stats.warning_count += p_check.native.warnings;
	r_output.append_array(p_check.native.output);
	r_stats.diagnostics.append_array(p_check.native.diagnostics);
	r_stats.script_count++;
	const GDScriptParser &parser = *p_check.reference->get_parser();
	for (const GDScriptWarning &warning : parser.get_warnings()) {
		EditorLanguage::Warning diagnostic;
		const auto location = parser.wgodot_source_location(warning.start_line);
		diagnostic.start_line = location.line;
		diagnostic.string_code = GDScriptWarning::get_name_from_code(warning.code);
		diagnostic.message = warning.get_message();
		r_stats.warning_count++;
		append_script_warning(diagnostic, location.path, r_stats, r_output);
	}
	int remaining = p_check.accepted_errors;
	for (const GDScriptParser::ParserError &parse_error : parser.get_errors()) {
		if (remaining-- == 0) {
			break;
		}
		EditorLanguage::ScriptError diagnostic;
		const auto location = parser.wgodot_source_location(parse_error.start_line);
		diagnostic.path = location.path;
		diagnostic.start_line = location.line;
		diagnostic.start_column = parse_error.start_column;
		diagnostic.message = parse_error.message;
		r_stats.error_count++;
		append_script_error(diagnostic, diagnostic.path, r_stats, r_output);
	}
}

} // namespace

Dictionary run_project_check_result(const ExportProject &p_project, const SafeFlag &p_cancelled, WGodotCheckBudget &p_budget) {
	WGodotCheckDiagnostics native{ &p_budget };
	WGodotErrorCapture capture(&WGodotCheckDiagnostics::capture, &native);
	Dictionary result;
	result["ok"] = true;
	result["command"] = "check";
	result["refreshed"] = true;
	PackedStringArray output;
	if (GDScriptLanguage::get_singleton() == nullptr) {
		output.push_back("wgodot-check: error: GDScript language is not initialized.");
		result["output"] = output;
		result["exit_code"] = 1;
		return result;
	}

	CheckStats stats;

	const uint64_t start_usec = OS::get_singleton()->get_ticks_usec();
	uint64_t parsed_usec = start_usec;
	uint64_t analyzed_usec = start_usec;
	uint64_t linted_usec = start_usec;
	{
		ExportContext context;
		ExportAnalysis analysis(p_project, context, false);
		const Vector<String> &paths = p_project.get_script_paths();
		const Vector<Ref<GDScriptParserRef>> parsers = analysis.prepare_parsers();
		Vector<ScriptCheck> scripts;
		scripts.resize(paths.size());
		ScriptCheck *checks = scripts.ptrw();
		for (int index = 0; index < paths.size(); index++) {
			checks[index].reference = parsers[index];
		}
		if (!scripts.is_empty()) {
			struct ParseWork {
				WGodotCheckBudget &budget;
				const SafeFlag &cancelled;
				void parse(uint32_t p_index, ScriptCheck *p_checks) {
					p_checks[p_index].parse(budget, cancelled);
				}
			} work{ p_budget, p_cancelled };
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			const WorkerThreadPool::GroupID group = pool->add_template_group_task(
					&work, &ParseWork::parse, checks, scripts.size(), -1, true, "Parse project scripts");
			pool->wait_for_group_task_completion(group);
		}
		parsed_usec = OS::get_singleton()->get_ticks_usec();
		ExportAnalysis::Scope scope(analysis);
		for (int index = 0; index < paths.size(); index++) {
			if (p_cancelled.is_set()) {
				return Dictionary();
			}
			if (p_budget.exhausted()) {
				break;
			}
			ScriptCheck &check = checks[index];
			if (check.error != OK) {
				continue;
			}
			check.reference = analysis.get_parser(paths[index], GDScriptParserRef::FULLY_SOLVED, check.error);
			if (check.error == OK) {
				check.error = check.reference->get_analyzer()->resolve_dependencies();
			}
			check.claim_errors(p_budget);
		}
		analyzed_usec = OS::get_singleton()->get_ticks_usec();
		if (!scripts.is_empty()) {
			// The linter reads immutable trees and writes only its parser's diagnostics.
			struct LintWork {
				WGodotCheckBudget &budget;
				void lint(uint32_t p_index, ScriptCheck *p_checks) {
					p_checks[p_index].lint(budget);
				}
			} work{ p_budget };
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			const WorkerThreadPool::GroupID group = pool->add_template_group_task(
					&work, &LintWork::lint, checks, scripts.size(), -1, false, "Lint project scripts");
			pool->wait_for_group_task_completion(group);
		}
		linted_usec = OS::get_singleton()->get_ticks_usec();
		for (int index = 0; index < paths.size(); index++) {
			if (checks[index].checked && checks[index].reference.is_valid()) {
				checks[index].claim_errors(p_budget);
			}
			append_script_diagnostics(checks[index], stats, output);
		}
	}
	output.append_array(native.output);
	stats.diagnostics.append_array(native.diagnostics);
	stats.error_count += native.errors;
	stats.warning_count += native.warnings;
	const uint64_t elapsed_msec = (OS::get_singleton()->get_ticks_usec() - start_usec) / 1000;

	output.push_back(vformat("wgodot-check: scanned %d GDScript file(s), found %d error(s), %d warning(s), %d directory error(s), %d read error(s) in %d ms.",
			stats.script_count,
			stats.error_count,
			stats.warning_count,
			stats.directory_error_count,
			stats.read_error_count,
			(int64_t)elapsed_msec));

	const int64_t parse_msec = (parsed_usec - start_usec) / 1000;
	const int64_t analysis_msec = (analyzed_usec - parsed_usec) / 1000;
	const int64_t lint_msec = (linted_usec - analyzed_usec) / 1000;
	output.push_back(vformat("wgodot-check: parse %d ms, analysis %d ms, lint %d ms.", parse_msec, analysis_msec, lint_msec));
	result["parse_msec"] = parse_msec;
	result["analysis_msec"] = analysis_msec;
	result["lint_msec"] = lint_msec;
	result["output"] = output;
	result["diagnostics"] = stats.diagnostics;
	result["exit_code"] = stats.error_count == 0 ? 0 : 1;
	result["script_count"] = stats.script_count;
	result["script_skipped_count"] = p_project.get_script_paths().size() - stats.script_count;
	result["error_count"] = stats.error_count;
	result["warning_count"] = stats.warning_count;
	result["directory_error_count"] = stats.directory_error_count;
	result["read_error_count"] = stats.read_error_count;
	result["elapsed_msec"] = static_cast<int64_t>(elapsed_msec);
	return result;
}

void ProjectCheck::run(void *p_userdata) {
	auto *check = static_cast<ProjectCheck *>(p_userdata);
	check->result = run_project_check_result(check->project, check->cancelled, *check->budget);
}

void ProjectCheck::start(std::shared_ptr<WGodotCheckBudget> p_budget) {
	Vector<String> paths;
	collect_script_paths(EditorFileSystem::get_singleton()->get_filesystem(), paths);
	start(paths, p_budget);
}

void ProjectCheck::start(const Vector<String> &p_paths, std::shared_ptr<WGodotCheckBudget> p_budget) {
	budget = p_budget ? p_budget : std::make_shared<WGodotCheckBudget>();
	HashSet<String> inputs;
	for (const String &path : p_paths) {
		inputs.insert(path);
	}
	// Capture source and class metadata before the worker owns the analysis.
	String error;
	if (project.capture(inputs, inputs, error) != OK) {
		result["ok"] = false;
		result["error"] = "source_read_failed";
		result["message"] = error;
		return;
	}
	task = WorkerThreadPool::get_singleton()->add_native_task(&ProjectCheck::run, this, false, "Check project scripts");
}

Dictionary ProjectCheck::poll() {
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		if (!pool->is_task_completed(task)) {
			return Dictionary();
		}
		pool->wait_for_task_completion(task);
		task = WorkerThreadPool::INVALID_TASK_ID;
	}
	return result;
}

ProjectCheck::~ProjectCheck() {
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		cancelled.set();
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
	}
}

} // namespace WGodotGDScriptCheckCLI
