// wgodot-changes::file
/**************************************************************************/
/*  gdscript_check_cli.cpp                                                */
/**************************************************************************/

#include "gdscript_check_cli.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"

#include "core/error/error_list.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptCheckCLI {

namespace {

struct CheckStats {
	int script_count = 0;
	int error_count = 0;
	int warning_count = 0;
	int directory_error_count = 0;
	int read_error_count = 0;
};

String get_error_name(Error p_error) {
	const int error_index = (int)p_error;
	if (error_index >= 0 && error_index < ERR_MAX) {
		return error_names[error_index];
	}
	return itos(error_index);
}

void collect_script_paths(const String &p_root_dir, Vector<String> &r_paths, CheckStats &r_stats, PackedStringArray &r_output) {
	Error err;
	Ref<DirAccess> dir = DirAccess::open(p_root_dir, &err);
	if (err != OK || dir.is_null()) {
		r_stats.error_count++;
		r_stats.directory_error_count++;
		r_output.push_back(vformat("%s: error: Unable to open directory (%s).", p_root_dir, get_error_name(err)));
		return;
	}

	if (dir->file_exists(".gdignore")) {
		return;
	}

	dir->list_dir_begin();
	String file_name = dir->get_next();
	while (!file_name.is_empty()) {
		if (file_name == "." || file_name == ".." || file_name == "./") {
			file_name = dir->get_next();
			continue;
		}

		if (dir->current_is_dir()) {
			collect_script_paths(p_root_dir.path_join(file_name), r_paths, r_stats, r_output);
		} else if (file_name.ends_with(".gd")) {
			r_paths.push_back(p_root_dir.path_join(file_name));
		}

		file_name = dir->get_next();
	}
	dir->list_dir_end();
}

void append_script_error(const ScriptLanguage::ScriptError &p_error, const String &p_fallback_path, PackedStringArray &r_output) {
	const String path = p_error.path.is_empty() ? p_fallback_path : p_error.path;
	const int line = p_error.line > 0 ? p_error.line : 0;
	const int column = p_error.column > 0 ? p_error.column : 0;
	r_output.push_back(vformat("%s:%d:%d: error: %s", path, line, column, p_error.message));
}

void append_script_warning(const ScriptLanguage::Warning &p_warning, const String &p_path, PackedStringArray &r_output) {
	const int line = p_warning.start_line > 0 ? p_warning.start_line : 0;
	r_output.push_back(vformat("%s:%d: warning (%s): %s", p_path, line, p_warning.string_code, p_warning.message));
}

void check_script(const String &p_path, CheckStats &r_stats, PackedStringArray &r_output) {
	r_stats.script_count++;

	Error read_error;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		r_stats.error_count++;
		r_stats.read_error_count++;
		r_output.push_back(vformat("%s: error: Unable to read script (%s).", p_path, get_error_name(read_error)));
		return;
	}

	List<ScriptLanguage::ScriptError> errors;
	List<ScriptLanguage::Warning> warnings;
	GDScriptLanguage::get_singleton()->validate(source, p_path, nullptr, &errors, &warnings, nullptr);

	for (const ScriptLanguage::ScriptError &error : errors) {
		r_stats.error_count++;
		append_script_error(error, p_path, r_output);
	}

	for (const ScriptLanguage::Warning &warning : warnings) {
		r_stats.warning_count++;
		append_script_warning(warning, p_path, r_output);
	}
}

} // namespace

Dictionary run_project_check_result() {
	Dictionary result;
	result["ok"] = true;
	result["command"] = "check";
	PackedStringArray output;
	if (GDScriptLanguage::get_singleton() == nullptr) {
		output.push_back("wgodot-check: error: GDScript language is not initialized.");
		result["output"] = output;
		result["exit_code"] = 1;
		return result;
	}

	CheckStats stats;
	Vector<String> script_paths;
	collect_script_paths("res://", script_paths, stats, output);
	script_paths.sort();

	// The editor filesystem updates ScriptServer's global-class registry, but
	// dependency analysis is served by a separate parser cache. Invalidate every
	// project parser so changed class interfaces and all transitive dependents are
	// rebuilt from disk during this check.
	for (const String &path : script_paths) {
		GDScriptCache::remove_parser(path);
	}

	const uint64_t start_usec = OS::get_singleton()->get_ticks_usec();
	for (const String &path : script_paths) {
		check_script(path, stats, output);
	}
	const uint64_t elapsed_msec = (OS::get_singleton()->get_ticks_usec() - start_usec) / 1000;

	output.push_back(vformat("wgodot-check: scanned %d GDScript file(s), found %d error(s), %d warning(s), %d directory error(s), %d read error(s) in %d ms.",
			stats.script_count,
			stats.error_count,
			stats.warning_count,
			stats.directory_error_count,
			stats.read_error_count,
			(int64_t)elapsed_msec));

	result["output"] = output;
	result["exit_code"] = stats.error_count == 0 ? 0 : 1;
	result["script_count"] = stats.script_count;
	result["error_count"] = stats.error_count;
	result["warning_count"] = stats.warning_count;
	result["directory_error_count"] = stats.directory_error_count;
	result["read_error_count"] = stats.read_error_count;
	result["elapsed_msec"] = static_cast<int64_t>(elapsed_msec);
	return result;
}

} // namespace WGodotGDScriptCheckCLI
