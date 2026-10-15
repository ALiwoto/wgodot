// wgodot-changes::file

#include "progress_dialog.h"

#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/wgodot_editor_activity.h"
#include "scene/main/window.h"

void ProgressDialog::_initialize_asset_progress() {
	asset_progress = memnew(VBoxContainer);
	main->add_child(asset_progress);
	asset_progress->hide();
	asset_title = memnew(Label);
	asset_progress->add_child(asset_title);
	asset_bar = memnew(ProgressBar);
	asset_bar->set_theme_type_variation("PopupProgressBar");
	asset_bar->set_indeterminate(true);
	asset_bar->set_editor_preview_indeterminate(true);
	asset_progress->add_child(asset_bar);
	asset_state = memnew(Label);
	asset_state->set_clip_text(true);
	asset_progress->add_child(asset_state);
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_process_internal(true);
}

void ProgressDialog::_set_input_blocked(bool p_blocked) {
	if (input_blocked == p_blocked) {
		return;
	}
	input_blocked = p_blocked;
	set_mouse_filter(p_blocked ? MOUSE_FILTER_STOP : MOUSE_FILTER_IGNORE);
	EditorNode::get_singleton()->set_process_input(p_blocked);
	for (ObjectID window_id : host_windows) {
		Window *host_window = ObjectDB::get_instance<Window>(window_id);
		if (host_window) {
			host_window->set_process_mode(p_blocked ? PROCESS_MODE_DISABLED : PROCESS_MODE_INHERIT);
		}
	}
}

bool ProgressDialog::_update_asset_progress() {
	if (EditorNode::is_cmdline_mode()) {
		return false;
	}
	const WGodotEditorActivity::Snapshot activity = WGodotEditorActivity::get_snapshot();
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	const bool processing_assets = filesystem && filesystem->is_processing_assets();
	const bool busy = processing_assets || activity.busy;
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (busy || (asset_progress_active && !tasks.is_empty())) {
		if (!asset_progress_active) {
			asset_progress_active = true;
			asset_started_msec = now;
		}
		asset_last_busy_msec = now;
		asset_title->set_text(vformat(TTR("Updating project (%ds)"), (now - asset_started_msec) / 1000));
		String stage = TTR("Processing editor tasks...");
		if (activity.busy) {
			stage = activity.stage;
		} else if (processing_assets) {
			stage = TTR("Scanning project files...");
		}
		asset_state->set_text(stage);
		asset_state->set_tooltip_text(activity.path);
		asset_bar->set_indeterminate(true);
	} else if (asset_progress_active) {
		if (now - asset_last_busy_msec < 750) {
			// Keep one display across deferred scan/import/refresh handoffs.
			asset_state->set_text(TTR("Finishing project updates..."));
		} else if (now - asset_last_busy_msec < 2750) {
			asset_title->set_text(TTR("Project updates finished"));
			asset_state->set_text(TTR("No asset processing tasks remain."));
			asset_state->set_tooltip_text(String());
			asset_bar->set_indeterminate(false);
			asset_bar->set_value(asset_bar->get_max());
		} else {
			asset_progress_active = false;
			asset_progress->hide();
			_set_input_blocked(false);
			hide();
		}
	}
	if (asset_progress_active) {
		asset_progress->show();
		asset_bar->set_visible(tasks.is_empty());
		if (tasks.is_empty()) {
			_set_input_blocked(false);
		}
		if (!is_visible()) {
			_popup(!tasks.is_empty());
		}
	}
	return asset_progress_active;
}

void ProgressDialog::refresh_worker_progress() {
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (now - last_worker_refresh_msec < 100 || MessageQueue::get_singleton()->is_flushing()) {
		return;
	}
	last_worker_refresh_msec = now;
	_update_ui();
}
