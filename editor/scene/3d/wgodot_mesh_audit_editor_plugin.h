// wgodot-changes::file
#pragma once

#include "editor/file_system/wgodot_mesh_collision_settings.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/resources/mesh.h"

class AcceptDialog;
class Button;
class CheckBox;
class Label;
class LineEdit;
class Node3D;
class PopupMenu;
class Tree;

class WGodotMeshAuditEditorPlugin : public EditorPlugin {
	GDCLASS(WGodotMeshAuditEditorPlugin, EditorPlugin);

	struct MeshDetails {
		int64_t triangles = 0;
		int64_t vertices = 0;
		int surfaces = 0;
	};
	struct CollisionDetails {
		int shapes = 0;
		int disabled_shapes = 0;
		int64_t triangles = 0;
		int64_t convex_vertices = 0;
		String description;
	};
	struct Entry {
		ObjectID node;
		String path;
		String source;
		String model_source;
		String mesh_key;
		MeshDetails mesh;
		CollisionDetails collision;
		Vector3 bounds;
		int instances = 1;
		int64_t file_bytes = -1;
		bool visible = true;
	};
	struct AssetDetails {
		WGodotMeshCollisionSettings collision;
		HashMap<String, String> mesh_keys;
	};
	enum CollisionChoice {
		NOT_NEEDED_FILE,
		NOT_NEEDED_MESH,
	};

	AcceptDialog *dialog = nullptr;
	LineEdit *search = nullptr;
	CheckBox *include_hidden = nullptr;
	CheckBox *include_not_needed = nullptr;
	PopupMenu *context_menu = nullptr;
	PopupMenu *collision_menu = nullptr;
	Tree *results = nullptr;
	Label *summary = nullptr;
	Label *details = nullptr;
	Label *scope = nullptr;
	Button *frame_button = nullptr;
	Button *asset_button = nullptr;
	Button *collision_button = nullptr;
	Vector<Entry> entries;
	HashMap<ObjectID, MeshDetails> mesh_cache;
	HashMap<ObjectID, CollisionDetails> collision_cache;
	HashMap<String, int64_t> file_size_cache;
	HashMap<String, AssetDetails> asset_cache;
	bool with_collision = false;
	int sort_column = 1;
	bool sort_descending = true;

	void _open(bool p_with_collision);
	void _refresh();
	void _collect(Node *p_node, Node *p_scene);
	const AssetDetails &_asset_details(const String &p_source);
	String _collision_decision(const Entry &p_entry) const;
	MeshDetails _mesh_details(const Ref<Mesh> &p_mesh);
	CollisionDetails _collision_details(Node *p_mesh, Node *p_scene);
	CollisionDetails _collision_subtree(Node *p_node);
	void _filter();
	void _search_changed(const String &p_text);
	void _hidden_changed(bool p_pressed);
	void _item_mouse_selected(const Vector2 &p_position, MouseButton p_button);
	void _collision_choice(int p_choice);
	void _sort(int p_column, int p_button);
	void _update_sort_headers();
	const Entry *_selected() const;
	void _selection_changed();
	void _frame_selected();
	void _show_asset();
	void _edit_collision();
	void _scene_changed(Node *p_root);

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "MeshAudit"; }
	WGodotMeshAuditEditorPlugin();
};
