// wgodot-changes::file
/**************************************************************************/
/*  wgodot_workspace_edit.cpp                                             */
/**************************************************************************/

#include "wgodot_workspace_edit.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

namespace WGodotWorkspaceEdit {

namespace {

struct TextEdit {
	int start_line = 0;
	int start_character = 0;
	int end_line = 0;
	int end_character = 0;
	String replacement;
};

struct ChangedFile {
	String path;
	String source;
	String changed_source;
	String temporary_path;
	String backup_path;
	int edit_count = 0;
};

struct EditDescendingComparator {
	_FORCE_INLINE_ bool operator()(const TextEdit &p_left, const TextEdit &p_right) const {
		if (p_left.start_line != p_right.start_line) {
			return p_left.start_line > p_right.start_line;
		}
		return p_left.start_character > p_right.start_character;
	}
};

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

String uri_to_path(const String &p_uri) {
	int port = 0;
	String scheme;
	String host;
	String encoded_path;
	String fragment;
	p_uri.parse_url(scheme, host, port, encoded_path, fragment);
	if (scheme != "file" && scheme != "file:" && scheme != "file://") {
		return String();
	}
	if (!host.is_empty() && host != "localhost") {
		return String();
	}
	String path = encoded_path.uri_file_decode().replace_char('\\', '/').simplify_path();
#ifdef WINDOWS_ENABLED
	if (path.length() >= 3 && path[0] == '/' && path[2] == ':') {
		path = path.substr(1);
	}
#endif
	return path;
}

String normalize_for_comparison(const String &p_path) {
	String path = p_path.replace_char('\\', '/').simplify_path().trim_suffix("/");
#ifdef WINDOWS_ENABLED
	path = path.to_lower();
#endif
	return path;
}

bool is_project_script(const String &p_path, const String &p_project_root) {
	const String path = normalize_for_comparison(p_path);
	const String root = normalize_for_comparison(p_project_root);
	return path.get_extension().to_lower() == "gd" && (path == root || path.begins_with(root + "/"));
}

bool position_to_offset(const String &p_source, int p_line, int p_character, int &r_offset) {
	if (p_line < 0 || p_character < 0) {
		return false;
	}
	int line = 0;
	int line_start = 0;
	for (int i = 0; i < p_source.length() && line < p_line; i++) {
		if (p_source[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}
	if (line != p_line) {
		return false;
	}
	int line_end = p_source.find_char('\n', line_start);
	if (line_end < 0) {
		line_end = p_source.length();
	}
	if (line_end > line_start && p_source[line_end - 1] == '\r') {
		line_end--;
	}
	if (p_character > line_end - line_start) {
		return false;
	}
	r_offset = line_start + p_character;
	return true;
}

bool parse_edit(const Dictionary &p_edit, TextEdit &r_edit) {
	if (!p_edit.has("range") || !p_edit.has("newText") || p_edit["range"].get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary range = p_edit["range"];
	if (!range.has("start") || !range.has("end") || range["start"].get_type() != Variant::DICTIONARY || range["end"].get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary start = range["start"];
	const Dictionary end = range["end"];
	r_edit.start_line = start.get("line", -1);
	r_edit.start_character = start.get("character", -1);
	r_edit.end_line = end.get("line", -1);
	r_edit.end_character = end.get("character", -1);
	r_edit.replacement = p_edit["newText"];
	return r_edit.start_line >= 0 && r_edit.start_character >= 0 && r_edit.end_line >= 0 && r_edit.end_character >= 0;
}

bool build_changed_file(const String &p_path, const Array &p_raw_edits, const String &p_expected_name, const String &p_new_name, ChangedFile &r_file, String &r_error) {
	Error read_error = OK;
	r_file.source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		r_error = "Could not read rename target: " + p_path;
		return false;
	}
	Vector<TextEdit> edits;
	for (const Variant &raw_edit : p_raw_edits) {
		if (raw_edit.get_type() != Variant::DICTIONARY) {
			r_error = "The language server returned an invalid text edit for: " + p_path;
			return false;
		}
		TextEdit edit;
		if (!parse_edit(raw_edit, edit) || edit.replacement != p_new_name) {
			r_error = "The language server returned an unexpected text edit for: " + p_path;
			return false;
		}
		edits.push_back(edit);
	}
	edits.sort_custom<EditDescendingComparator>();

	String changed = r_file.source;
	int previous_start = changed.length();
	for (const TextEdit &edit : edits) {
		int start_offset = 0;
		int end_offset = 0;
		if (!position_to_offset(r_file.source, edit.start_line, edit.start_character, start_offset) ||
				!position_to_offset(r_file.source, edit.end_line, edit.end_character, end_offset) ||
				end_offset < start_offset || end_offset > previous_start) {
			r_error = "The language server returned an invalid or overlapping range for: " + p_path;
			return false;
		}
		if (r_file.source.substr(start_offset, end_offset - start_offset) != p_expected_name) {
			r_error = "A rename target changed on disk before edits were applied: " + p_path;
			return false;
		}
		changed = changed.substr(0, start_offset) + edit.replacement + changed.substr(end_offset);
		previous_start = start_offset;
	}
	r_file.path = p_path;
	r_file.changed_source = changed;
	r_file.edit_count = edits.size();
	return true;
}

void remove_if_present(const String &p_path) {
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
}

bool write_temporary_files(Vector<ChangedFile> &r_files, String &r_error) {
	const String suffix = ".wgodot-rename-" + itos(OS::get_singleton()->get_process_id()) + "-" + itos(OS::get_singleton()->get_ticks_usec());
	for (int i = 0; i < r_files.size(); i++) {
		ChangedFile &file = r_files.write[i];
		file.temporary_path = file.path + suffix + ".tmp";
		file.backup_path = file.path + suffix + ".bak";
		Ref<FileAccess> output = FileAccess::open(file.temporary_path, FileAccess::WRITE);
		if (output.is_null()) {
			r_error = "Could not create a temporary rename file beside: " + file.path;
			for (const ChangedFile &cleanup : r_files) {
				remove_if_present(cleanup.temporary_path);
			}
			return false;
		}
		output->store_string(file.changed_source);
		output->flush();
		if (output->get_error() != OK) {
			r_error = "Could not finish writing a temporary rename file beside: " + file.path;
			for (const ChangedFile &cleanup : r_files) {
				remove_if_present(cleanup.temporary_path);
			}
			return false;
		}
	}
	return true;
}

bool install_temporary_files(Vector<ChangedFile> &r_files, String &r_error) {
	int backed_up = 0;
	for (; backed_up < r_files.size(); backed_up++) {
		if (DirAccess::rename_absolute(r_files[backed_up].path, r_files[backed_up].backup_path) != OK) {
			r_error = "Could not prepare a rename target for atomic replacement: " + r_files[backed_up].path;
			for (int i = backed_up - 1; i >= 0; i--) {
				DirAccess::rename_absolute(r_files[i].backup_path, r_files[i].path);
			}
			for (const ChangedFile &file : r_files) {
				remove_if_present(file.temporary_path);
			}
			return false;
		}
	}

	int installed = 0;
	for (; installed < r_files.size(); installed++) {
		if (DirAccess::rename_absolute(r_files[installed].temporary_path, r_files[installed].path) != OK) {
			r_error = "Could not atomically install a renamed file: " + r_files[installed].path;
			for (int i = 0; i < installed; i++) {
				remove_if_present(r_files[i].path);
			}
			for (int i = 0; i < r_files.size(); i++) {
				DirAccess::rename_absolute(r_files[i].backup_path, r_files[i].path);
				remove_if_present(r_files[i].temporary_path);
			}
			return false;
		}
	}
	for (const ChangedFile &file : r_files) {
		remove_if_present(file.backup_path);
	}
	return true;
}

} // namespace

Dictionary apply(const Dictionary &p_workspace_edit, const String &p_project_root, const String &p_expected_name, const String &p_new_name, bool p_dry_run) {
	if (!p_workspace_edit.has("changes") || p_workspace_edit["changes"].get_type() != Variant::DICTIONARY) {
		return make_error("unsupported_workspace_edit", "The language server returned an unsupported workspace edit format.");
	}
	const Dictionary changes = p_workspace_edit["changes"];
	Vector<ChangedFile> changed_files;
	int total_edits = 0;
	for (const Variant &uri_variant : changes.keys()) {
		const String uri = uri_variant;
		const String path = uri_to_path(uri);
		if (path.is_empty() || !is_project_script(path, p_project_root)) {
			return make_error("unsafe_edit_path", "The language server tried to edit a file outside this project's GDScript sources: " + uri);
		}
		if (changes[uri_variant].get_type() != Variant::ARRAY) {
			return make_error("invalid_workspace_edit", "The language server returned an invalid edit list for: " + path);
		}
		ChangedFile file;
		String error_message;
		if (!build_changed_file(path, changes[uri_variant], p_expected_name, p_new_name, file, error_message)) {
			return make_error("workspace_edit_validation_failed", error_message);
		}
		total_edits += file.edit_count;
		changed_files.push_back(file);
	}
	if (changed_files.is_empty() || total_edits == 0) {
		return make_error("no_rename_edits", "The language server found no semantic rename edits.");
	}

	if (!p_dry_run) {
		String error_message;
		if (!write_temporary_files(changed_files, error_message) || !install_temporary_files(changed_files, error_message)) {
			return make_error("workspace_edit_apply_failed", error_message);
		}
	}

	PackedStringArray files;
	for (const ChangedFile &file : changed_files) {
		files.push_back(file.path);
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "rename";
	response["dry_run"] = p_dry_run;
	response["old_name"] = p_expected_name;
	response["new_name"] = p_new_name;
	response["file_count"] = changed_files.size();
	response["edit_count"] = total_edits;
	response["files"] = files;
	return response;
}

} // namespace WGodotWorkspaceEdit
