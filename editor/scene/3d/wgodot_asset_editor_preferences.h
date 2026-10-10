// wgodot-changes::file
#pragma once

#include "editor/plugins/editor_plugin.h"

class WGodotAssetEditorPreferences : public EditorPlugin {
	GDCLASS(WGodotAssetEditorPreferences, EditorPlugin);

	static WGodotAssetEditorPreferences *singleton;
	ObjectID active_root;
	String active_key;
	Dictionary observed_state;
	Dictionary saved_state;
	uint64_t last_change_msec = 0;
	bool storage_failed = false;

	String _asset_key(Node *p_root) const;
	Dictionary _capture() const;
	void _scene_changed(Node *p_root);
	void _scene_saved(const String &p_path);
	void _poll();
	void _storage_error(Error p_error);

protected:
	void _notification(int p_what);

public:
	static WGodotAssetEditorPreferences *get_singleton() { return singleton; }
	void save_current();
	virtual void save_external_data() override;
	virtual String get_plugin_name() const override { return "AssetEditorPreferences"; }
	WGodotAssetEditorPreferences();
	~WGodotAssetEditorPreferences();
};
