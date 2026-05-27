// wgodot-changes::file
/**************************************************************************/
/*  wgodot_editor_toaster.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "editor_toaster.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/resources/text_file.h"

static bool wgodot_is_script_path(const String &p_path) {
	return ScriptServer::get_language_for_extension(p_path.get_extension()) != nullptr;
}

static bool wgodot_extract_failed_load_script_path(const String &p_message, String &r_path) {
	const String marker = "Failed to load script \"";
	const int marker_pos = p_message.find(marker);
	if (marker_pos < 0) {
		return false;
	}

	const int path_begin = marker_pos + marker.length();
	const int path_end = p_message.find("\"", path_begin);
	if (path_end < 0) {
		return false;
	}

	r_path = ProjectSettings::get_singleton()->localize_path(p_message.substr(path_begin, path_end - path_begin));
	return wgodot_is_script_path(r_path);
}

static bool wgodot_extract_tooltip_script_location(const String &p_tooltip, String &r_path, int &r_line, int &r_column) {
	const int separator = p_tooltip.rfind(":");
	if (separator < 0) {
		return false;
	}

	const String path = ProjectSettings::get_singleton()->localize_path(p_tooltip.substr(0, separator));
	if (!wgodot_is_script_path(path)) {
		return false;
	}

	r_path = path;
	r_line = p_tooltip.substr(separator + 1).to_int();
	r_column = 0;
	return r_line > 0;
}

static bool wgodot_find_script_error_location(String &r_path, int &r_line, int &r_column) {
	if (!wgodot_is_script_path(r_path)) {
		return false;
	}

	const String remapped_path = ResourceLoader::path_remap(r_path);
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(remapped_path, FileAccess::READ, &err);
	if (err != OK || file.is_null()) {
		return r_line > 0;
	}

	const String source = file->get_as_utf8_string();
	ScriptLanguage *language = ScriptServer::get_language_for_extension(r_path.get_extension());
	if (language == nullptr) {
		return r_line > 0;
	}

	List<ScriptLanguage::ScriptError> errors;
	if (language->validate(source, r_path, nullptr, &errors) || errors.is_empty()) {
		return r_line > 0;
	}

	ScriptLanguage::ScriptError first_error;
	bool has_first_error = false;
	for (const ScriptLanguage::ScriptError &error : errors) {
		if (!has_first_error) {
			first_error = error;
			has_first_error = true;
		}
		if (error.path.is_empty() || ProjectSettings::get_singleton()->localize_path(error.path) == r_path) {
			first_error = error;
			break;
		}
	}

	ERR_FAIL_COND_V(!has_first_error, r_line > 0);

	if (!first_error.path.is_empty()) {
		r_path = ProjectSettings::get_singleton()->localize_path(first_error.path);
	}
	r_line = first_error.line;
	r_column = first_error.column;
	return r_line > 0;
}

static Ref<Resource> wgodot_load_script_text_file(const String &p_path) {
	Ref<TextFile> text_file;
	text_file.instantiate();

	const String local_path = ProjectSettings::get_singleton()->localize_path(p_path);
	const String remapped_path = ResourceLoader::path_remap(local_path);
	if (text_file->load_text(remapped_path) != OK) {
		return Ref<Resource>();
	}

	text_file->set_file_path(local_path);
	text_file->set_path(local_path, true);
	if (ResourceLoader::get_timestamp_on_load()) {
		text_file->set_last_modified_time(FileAccess::get_modified_time(remapped_path));
	}

	return text_file;
}

void EditorToaster::wgodot_update_toast_script_location(Control *p_control, const String &p_message, const String &p_tooltip) {
	ERR_FAIL_NULL(p_control);
	ERR_FAIL_COND(!toasts.has(p_control));

	Toast &toast = toasts[p_control];
	toast.wgodot_script_path = String();
	toast.wgodot_script_line = -1;
	toast.wgodot_script_column = 0;
	p_control->set_default_cursor_shape(Control::CURSOR_ARROW);

	String script_path;
	int line = -1;
	int column = 0;
	bool has_location = wgodot_extract_tooltip_script_location(p_tooltip, script_path, line, column);
	if (!has_location) {
		has_location = wgodot_extract_failed_load_script_path(p_message, script_path);
	}
	if (!has_location || !wgodot_find_script_error_location(script_path, line, column)) {
		p_control->set_tooltip_text(p_tooltip);
		return;
	}

	toast.wgodot_script_path = script_path;
	toast.wgodot_script_line = line;
	toast.wgodot_script_column = column;
	p_control->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
	p_control->set_tooltip_text(vformat("%s:%d", script_path, line));
}

void EditorToaster::wgodot_open_toast_script_location(const Ref<InputEvent> &p_event, Control *p_control) {
	ERR_FAIL_NULL(p_control);
	ERR_FAIL_COND(!toasts.has(p_control));

	Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_null() || !mouse_button->is_pressed() || mouse_button->get_button_index() != MouseButton::LEFT) {
		return;
	}

	const Toast &toast = toasts[p_control];
	if (toast.wgodot_script_path.is_empty() || toast.wgodot_script_line <= 0) {
		return;
	}

	Ref<Resource> resource = wgodot_load_script_text_file(toast.wgodot_script_path);
	if (resource.is_null() || ScriptEditor::get_singleton() == nullptr) {
		return;
	}

	EditorNode::get_singleton()->get_editor_main_screen()->select(EditorMainScreen::EDITOR_SCRIPT);
	ScriptEditor::get_singleton()->edit(resource, toast.wgodot_script_line - 1, MAX(toast.wgodot_script_column - 1, 0), true);
	p_control->accept_event();
}
