// wgodot-changes::file
#include "wgodot_asset_editor_preferences.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/scene/3d/wgodot_mesh_collision_editor_plugin.h"
#include "editor/settings/wgodot_asset_editor_settings.h"
#include "scene/main/timer.h"

WGodotAssetEditorPreferences *WGodotAssetEditorPreferences::singleton = nullptr;

String WGodotAssetEditorPreferences::_asset_key(Node *p_root) const {
	const String path = p_root->get_scene_file_path();
	if (path.is_empty()) {
		return String();
	}
	const ResourceUID::ID uid = ResourceLoader::get_resource_uid(path);
	return uid == ResourceUID::INVALID_ID ? path : ResourceUID::get_singleton()->id_to_text(uid);
}

Dictionary WGodotAssetEditorPreferences::_capture() const {
	Dictionary result;
	Dictionary view = Node3DEditor::get_singleton()->get_state();
	Array viewports = view["viewports"];
	for (int i = 0; i < viewports.size(); i++) {
		Dictionary viewport = viewports[i];
		// A live scene-camera preview is transient, not a personal viewport preference.
		viewport.erase("previewing");
	}
	result["3D"] = view;
	result["collision"] = WGodotMeshCollisionEditorPlugin::get_singleton()->get_personal_state();
	return result;
}

void WGodotAssetEditorPreferences::_scene_changed(Node *p_root) {
	if (p_root && p_root->get_instance_id() == active_root) {
		return;
	}
	active_root = ObjectID();
	active_key = String();
	observed_state.clear();
	saved_state.clear();
	if (!Object::cast_to<Node3D>(p_root)) {
		return;
	}
	active_root = p_root->get_instance_id();
	active_key = _asset_key(p_root);
	Dictionary stored;
	if (!active_key.is_empty() && !storage_failed) {
		const Error error = WGodotAssetEditorSettings::load(active_key, stored);
		if (error != OK && error != ERR_FILE_NOT_FOUND && error != ERR_DOES_NOT_EXIST) {
			_storage_error(error);
		}
	}
	// Scene plugin callbacks run after Godot restores its ordinary scene state.
	// This plugin is registered after the collision reference has been created.
	if (stored.has("3D")) {
		Node3DEditor::get_singleton()->set_state(stored["3D"]);
	}
	WGodotMeshCollisionEditorPlugin::get_singleton()->set_personal_state(stored.get("collision", Dictionary()));
	observed_state = _capture();
	if (!stored.is_empty()) {
		saved_state = observed_state;
	}
	last_change_msec = OS::get_singleton()->get_ticks_msec();
}

void WGodotAssetEditorPreferences::_scene_saved(const String &p_path) {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (root && root->get_scene_file_path() == p_path) {
		save_current();
	}
}

void WGodotAssetEditorPreferences::_poll() {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (!root || root->get_instance_id() != active_root || EditorNode::get_singleton()->is_changing_scene() || !Node3DEditor::get_singleton()->is_visible_in_tree()) {
		return;
	}
	const Dictionary state = _capture();
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (state != observed_state) {
		observed_state = state;
		last_change_msec = now;
		return;
	}
	for (uint32_t i = 0; i < Node3DEditor::VIEWPORTS_COUNT; i++) {
		if (Node3DEditor::get_singleton()->get_editor_viewport(i)->get_controller()->is_navigating()) {
			last_change_msec = now;
			return;
		}
	}
	if (!storage_failed && state != saved_state && now - last_change_msec >= 1500) {
		save_current();
	}
}

void WGodotAssetEditorPreferences::_storage_error(Error p_error) {
	storage_failed = true;
	ERR_PRINT(vformat("Could not access .godot/assets_editor_personal_params.dat (%s). Personal asset preference writes are disabled for this editor session; the file has been preserved.", error_names[p_error]));
}

void WGodotAssetEditorPreferences::save_current() {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (storage_failed || !root || root->get_instance_id() != active_root) {
		return;
	}
	const String key = _asset_key(root);
	if (key.is_empty()) {
		return;
	}
	const Dictionary state = _capture();
	if (key == active_key && state == saved_state) {
		return;
	}
	const Error error = WGodotAssetEditorSettings::save(key, state);
	if (error != OK) {
		_storage_error(error);
		return;
	}
	active_key = key;
	observed_state = state;
	saved_state = state;
}

void WGodotAssetEditorPreferences::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		connect("scene_changed", callable_mp(this, &WGodotAssetEditorPreferences::_scene_changed));
		connect("scene_saved", callable_mp(this, &WGodotAssetEditorPreferences::_scene_saved));
	}
}

void WGodotAssetEditorPreferences::save_external_data() {
	// Editor shutdown calls this before it destroys the edited scene roots.
	save_current();
}

WGodotAssetEditorPreferences::WGodotAssetEditorPreferences() {
	singleton = this;
	Timer *timer = memnew(Timer);
	timer->set_wait_time(0.5);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &WGodotAssetEditorPreferences::_poll));
	add_child(timer);
}

WGodotAssetEditorPreferences::~WGodotAssetEditorPreferences() {
	singleton = nullptr;
}
