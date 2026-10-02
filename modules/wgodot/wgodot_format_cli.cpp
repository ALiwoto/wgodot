// wgodot-changes::file

#include "wgodot_format_cli.h"

#include "wgodot_cli.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/string/string_builder.h"
#include "core/templates/hash_set.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/wgodot_gdscript_formatter.h"

#include "modules/gdscript/gdscript_cache.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

namespace WGodotFormatCLI {

namespace {

struct Options {
	WGodotGDScriptFormatter::Options formatting;
	bool ignore_generated_files = true;
};

Dictionary failure(const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["message"] = p_message;
	return response;
}

bool load_options(const String &p_root, const Dictionary &p_overrides, Options &r_options, String &r_error) {
	const String path = p_root.path_join("wgformat.cfg");
	if (FileAccess::exists(path)) {
		ConfigFile config;
		if (config.load(path) != OK) {
			r_error = "Cannot read formatter configuration: " + path;
			return false;
		}
		for (const String &section : config.get_sections()) {
			if (section != "format") {
				r_error = "wgformat.cfg: unknown section [" + section + "]. Expected [format].";
				return false;
			}
			for (const String &key : config.get_section_keys(section)) {
				const Variant value = config.get_value(section, key);
				if (key == "line_length") {
					if (value.get_type() != Variant::INT || int64_t(value) < 20 || int64_t(value) > 320) {
						r_error = "wgformat.cfg: line_length must be an integer between 20 and 320.";
						return false;
					}
					r_options.formatting.line_length = value;
				} else if (key == "explicit_self" || key == "ignore_generated_files") {
					if (value.get_type() != Variant::BOOL) {
						r_error = "wgformat.cfg: " + key + " must be true or false.";
						return false;
					}
					if (key == "explicit_self") {
						r_options.formatting.explicit_self = value;
					} else {
						r_options.ignore_generated_files = value;
					}
				} else {
					r_error = "wgformat.cfg: unknown formatter option: " + key;
					return false;
				}
			}
		}
	}
	if (p_overrides.has("line_length")) {
		const Variant width = p_overrides["line_length"];
		// JSON transports numbers as doubles, including integer CLI arguments.
		if ((width.get_type() != Variant::INT && width.get_type() != Variant::FLOAT) ||
				double(width) < 20 || double(width) > 320 || double(width) != int64_t(width)) {
			r_error = "line_length must be an integer between 20 and 320.";
			return false;
		}
		r_options.formatting.line_length = width;
	}
	return true;
}

bool is_generated(const String &p_source) {
	bool generated = false;
	bool do_not_edit = false;
	int start = 0;
	while (start < p_source.length()) {
		const int newline = p_source.find_char('\n', start);
		const int end = newline < 0 ? p_source.length() : newline;
		const String line = p_source.substr(start, end - start).strip_edges();
		start = end + 1;
		if (line.is_empty()) {
			continue;
		}
		if (!line.begins_with("#")) {
			break;
		}
		const String comment = line.to_lower();
		generated |= comment.contains("generated");
		do_not_edit |= comment.contains("do not edit");
		if (generated && do_not_edit) {
			return true;
		}
	}
	return false;
}

bool collect_scripts(const String &p_path, HashSet<String> &r_files, String &r_error, bool p_explicit = true) {
	if (FileAccess::exists(p_path)) {
		if (p_path.get_extension() != "gd") {
			r_error = "Expected a .gd file or directory: " + p_path;
			return false;
		}
		r_files.insert(p_path);
		return true;
	}
	Error error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_path, &error);
	if (directory.is_null()) {
		r_error = "Cannot open directory: " + p_path;
		return false;
	}
	if (FileAccess::exists(p_path.path_join(".gdignore")) || (!p_explicit && FileAccess::exists(p_path.path_join("project.godot")))) {
		return true;
	}
	if (directory->list_dir_begin() != OK) {
		r_error = "Cannot list directory: " + p_path;
		return false;
	}
	for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
		if (name.begins_with(".") || directory->is_link(name)) {
			continue;
		}
		const String path = p_path.path_join(name);
		if (directory->current_is_dir()) {
			if (!collect_scripts(path, r_files, r_error, false)) {
				return false;
			}
		} else if (name.get_extension() == "gd") {
			r_files.insert(path);
		}
	}
	return true;
}

String unified_diff(const String &p_path, const String &p_before, const String &p_after) {
	PackedStringArray before = p_before.split("\n");
	PackedStringArray after = p_after.split("\n");
	if (p_before.ends_with("\n")) {
		before.resize(before.size() - 1);
	}
	if (p_after.ends_with("\n")) {
		after.resize(after.size() - 1);
	}
	int prefix = 0;
	while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) {
		prefix++;
	}
	int suffix = 0;
	while (suffix < before.size() - prefix && suffix < after.size() - prefix && before[before.size() - suffix - 1] == after[after.size() - suffix - 1]) {
		suffix++;
	}
	const int start = MAX(0, prefix - 3);
	const int context = MIN(3, suffix);
	const int before_end = before.size() - suffix;
	const int after_end = after.size() - suffix;
	StringBuilder diff;
	diff.append("--- " + p_path + "\n+++ " + p_path + "\n");
	diff.append(vformat("@@ -%d,%d +%d,%d @@\n", before.is_empty() ? 0 : start + 1, before_end + context - start, after.is_empty() ? 0 : start + 1, after_end + context - start));
	for (int i = start; i < prefix; i++) {
		diff.append(" " + before[i] + "\n");
	}
	for (int i = prefix; i < before_end; i++) {
		diff.append("-" + before[i] + "\n");
		if (i + 1 == before.size() && !p_before.ends_with("\n")) {
			diff.append("\\ No newline at end of file\n");
		}
	}
	for (int i = prefix; i < after_end; i++) {
		diff.append("+" + after[i] + "\n");
	}
	for (int i = 0; i < context; i++) {
		diff.append(" " + before[before_end + i] + "\n");
	}
	return diff.as_string();
}

bool write_script(const String &p_path, const String &p_before, const String &p_after, String &r_error) {
	Error error = OK;
	if (FileAccess::get_file_as_string(p_path, &error) != p_before || error != OK) {
		r_error = "File changed during formatting: " + p_path;
		return false;
	}
	// The temporary output is on the same filesystem. No source backup is made.
	const String temporary = p_path + ".wg-format-" + itos(OS::get_singleton()->get_process_id()) + ".tmp";
	if (FileAccess::exists(temporary)) {
		r_error = "Temporary output already exists: " + temporary;
		return false;
	}
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &error);
	if (file.is_null()) {
		r_error = "Cannot write formatter output: " + temporary;
		return false;
	}
	file->store_string(p_after);
	file->flush();
	error = file->get_error();
	file.unref();
	if (error == OK) {
#ifdef WINDOWS_ENABLED
		// DirAccessWindows::rename removes its destination before moving, which
		// could lose the original on failure. ReplaceFile keeps it intact.
		const Char16String destination = p_path.replace_char('/', '\\').utf16();
		const Char16String replacement = temporary.replace_char('/', '\\').utf16();
		error = ReplaceFileW(reinterpret_cast<LPCWSTR>(destination.get_data()), reinterpret_cast<LPCWSTR>(replacement.get_data()), nullptr, 0, nullptr, nullptr) ? OK : ERR_FILE_CANT_WRITE;
#else
		error = DirAccess::rename_absolute(temporary, p_path);
#endif
	}
	if (error != OK) {
		DirAccess::remove_absolute(temporary);
		r_error = "Cannot replace formatted script: " + p_path;
		return false;
	}
	return true;
}

void print_help() {
	print_line("Usage: wg format [paths...] [--check|--diff] [--line-length <columns>] [--json]");
	print_line("Formats GDScript through the running project editor. Defaults to the whole project.");
	print_line("  --check        Report files needing formatting; do not write. Exit 1 if any differ.");
	print_line("  --diff         Print a unified diff; do not write. Exit 1 if any differ.");
	print_line("  --line-length  Override the configured line width (default: 88). Uses tabs.");
	print_line("  --json         Print structured results. Errors return a nonzero exit status.");
	print_line("Respects # fmt: off/on and # fmt: skip. Skips hidden directories and .gdignore.");
	print_line("Reads wgformat.cfg beside project.godot. Defaults apply when it is absent:");
	print_line("  [format]\n  line_length=88\n  explicit_self=true\n  ignore_generated_files=true");
	print_line("explicit_self qualifies resolved instance members, preserving property accessor storage.");
	print_line("ignore_generated_files skips files whose leading comments contain both 'generated' and");
	print_line("'do not edit' (case-insensitive), including explicitly selected files. Set false to format them.");
}

} // namespace

int run(const Vector<String> &p_arguments) {
	bool check = false;
	bool diff = false;
	bool json = false;
	bool paths_only = false;
	bool width_override = false;
	int width = 88;
	PackedStringArray paths;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (!paths_only && (argument == "--help" || argument == "-h")) {
			print_help();
			return 0;
		} else if (!paths_only && argument == "--") {
			paths_only = true;
		} else if (!paths_only && argument == "--check") {
			check = true;
		} else if (!paths_only && argument == "--diff") {
			diff = true;
		} else if (!paths_only && argument == "--json") {
			json = true;
		} else if (!paths_only && argument == "--line-length") {
			if (i + 1 == p_arguments.size() || !p_arguments[i + 1].is_valid_int()) {
				print_line("wgodot: --line-length requires an integer between 20 and 320.");
				return 2;
			}
			width = p_arguments[++i].to_int();
			width_override = true;
		} else if (!paths_only && argument.begins_with("-")) {
			print_line("wgodot: unknown format option: " + argument);
			return 2;
		} else {
			paths.push_back(argument.is_absolute_path() || argument.begins_with("res://") ? argument : OS::get_singleton()->get_cwd().path_join(argument));
		}
	}
	if (width < 20 || width > 320 || (check && diff)) {
		print_line("wgodot: use either --check or --diff, with a line length between 20 and 320.");
		return 2;
	}
	Dictionary options;
	options["paths"] = paths;
	options["check"] = check;
	options["diff"] = diff;
	if (width_override) {
		options["line_length"] = width;
	}
	Dictionary request;
	request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	request["command"] = "format";
	request["options"] = options;
	Dictionary response;
	const int connection_result = WGodotCLI::request_editor_command(request, response);
	if (json) {
		print_line(JSON::stringify(response, "", true));
	} else if (connection_result != 0 || !response.has("files")) {
		print_line("wgodot: " + String(response.get("message", "Could not format scripts.")));
	} else {
		const Array files = response["files"];
		for (const Dictionary &file : files) {
			if (file.has("error")) {
				print_line(String(file["path"]) + ": " + String(file["error"]));
			} else if (file.has("diff")) {
				print_line(String(file["diff"]).trim_suffix("\n"));
			} else if ((bool)file.get("changed", false)) {
				print_line(String(check ? "Would reformat: " : "Reformatted: ") + String(file["path"]));
			}
		}
		print_line(vformat("%d file(s) %s, %d unchanged, %d generated skipped, %d failed.", (int)response.get("changed", 0), check || diff ? "would be reformatted" : "reformatted", (int)response.get("unchanged", 0), (int)response.get("skipped", 0), (int)response.get("failed", 0)));
	}
	if (connection_result != 0) {
		return connection_result;
	}
	if (!(bool)response.get("ok", false)) {
		return 2;
	}
	return (check || diff) && (int)response.get("changed", 0) > 0 ? 1 : 0;
}

Dictionary execute(const Dictionary &p_options) {
	const bool check = p_options.get("check", false);
	const bool diff = p_options.get("diff", false);
	Options options;
	if (check && diff) {
		return failure("Invalid formatter options.");
	}
	const String root = WGodotCLI::get_current_project_root();
	String error;
	if (!load_options(root, p_options, options, error)) {
		return failure(error);
	}
	PackedStringArray paths = p_options.get("paths", PackedStringArray());
	if (paths.is_empty()) {
		paths.push_back(root);
	}
	HashSet<String> selected;
	for (const String &path : paths) {
		const String absolute = ProjectSettings::get_singleton()->globalize_path(path).replace_char('\\', '/').simplify_path();
		if (absolute != root && !absolute.begins_with(root + "/")) {
			return failure("Format paths must belong to the connected editor project: " + path);
		}
		if (!collect_scripts(absolute, selected, error)) {
			return failure(error);
		}
	}
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!check && !diff && script_editor) {
		for (const String &path : script_editor->get_unsaved_files()) {
			if (!selected.has(ProjectSettings::get_singleton()->globalize_path(path))) {
				continue;
			}
			if (options.ignore_generated_files && is_generated(FileAccess::get_file_as_string(path))) {
				continue;
			}
			return failure("Save the script open in the Godot editor before formatting it: " + path);
		}
	}
	Vector<String> scripts;
	for (const String &path : selected) {
		scripts.push_back(path);
	}
	scripts.sort();
	if (options.formatting.explicit_self) {
		// Selected scripts can inherit members from any project script. Resolve
		// those interfaces from disk even when only formatting a single file.
		HashSet<String> dependencies(selected);
		if (!collect_scripts(root, dependencies, error)) {
			return failure(error);
		}
		for (const String &path : dependencies) {
			GDScriptCache::remove_parser(ProjectSettings::get_singleton()->localize_path(path));
		}
	}
	Array results;
	int changed = 0;
	int unchanged = 0;
	int skipped = 0;
	int failed = 0;
	for (const String &path : scripts) {
		const String local = ProjectSettings::get_singleton()->localize_path(path);
		Dictionary result;
		result["path"] = local;
		Error read_error = OK;
		const String before = FileAccess::get_file_as_string(path, &read_error);
		String after;
		error.clear();
		if (read_error != OK) {
			error = "Cannot read script.";
		} else if (options.ignore_generated_files && is_generated(before)) {
			skipped++;
			result["skipped"] = true;
			result["reason"] = "generated";
		} else if (WGodotGDScriptFormatter::format(before, local, options.formatting, after, error)) {
			if (before == after) {
				unchanged++;
			} else if (check || diff || write_script(path, before, after, error)) {
				changed++;
				result["changed"] = true;
				if (diff) {
					result["diff"] = unified_diff(local, before, after);
				}
			}
		}
		if (!error.is_empty()) {
			failed++;
			result["error"] = error;
		}
		results.push_back(result);
	}
	if (changed > 0 && !check && !diff) {
		if (EditorFileSystem::get_singleton()) {
			EditorFileSystem::get_singleton()->scan_changes();
		}
		if (script_editor) {
			script_editor->reload_open_files();
		}
	}
	Dictionary response;
	response["ok"] = failed == 0;
	response["command"] = "format";
	response["changed"] = changed;
	response["unchanged"] = unchanged;
	response["skipped"] = skipped;
	response["failed"] = failed;
	response["files"] = results;
	if (diff && JSON::stringify(response).utf8().length() >= 4 * 1024 * 1024) {
		return failure("The diff exceeds the editor connection's 4 MiB limit. Select fewer files or use --check.");
	}
	return response;
}

} // namespace WGodotFormatCLI
