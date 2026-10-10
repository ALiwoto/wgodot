// wgodot-changes::file
#pragma once

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/resources/mesh.h"

class HBoxContainer;
class MenuButton;
class Label;
class SpinBox;
class ConfirmationDialog;
class StaticBody3D;
class Shape3D;
class Node3D;

class WGodotMeshCollisionEditorPlugin : public EditorPlugin {
	GDCLASS(WGodotMeshCollisionEditorPlugin, EditorPlugin);

	enum CollisionOption {
		BOX,
		SPHERE,
		CAPSULE,
		CYLINDER,
		CONVEX,
		AUTO_CONVEX,
		DELETE_CUSTOM_COLLISIONS,
	};
	enum ShowOption {
		SHOW_MESH,
	};
	static WGodotMeshCollisionEditorPlugin *singleton;
	ObjectID edited_root;
	ObjectID preview_root;
	AABB mesh_bounds;
	Vector<Vector3> mesh_faces;
	HBoxContainer *toolbar = nullptr;
	MenuButton *collision_menu = nullptr;
	MenuButton *show_menu = nullptr;
	Label *status = nullptr;
	ConfirmationDialog *convex_dialog = nullptr;
	ConfirmationDialog *delete_dialog = nullptr;
	String deletion_path;
	String deletion_source;
	ObjectID deletion_root;
	SpinBox *hull_count = nullptr;
	SpinBox *hull_vertices = nullptr;
	SpinBox *precision = nullptr;

	Thread decomposition_thread;
	SafeFlag decomposition_done;
	ObjectID decomposition_root;
	Vector<real_t> decomposition_vertices;
	Vector<uint32_t> decomposition_indices;
	Ref<MeshConvexDecompositionSettings> decomposition_settings;
	Vector<Vector<Vector3>> decomposition_hulls;

	void _scene_changed(Node *p_root);
	void _scene_saved(const String &p_path);
	void _reimport_asset(const String &p_source);
	void _clear_preview();
	void _collect_meshes(Node *p_node, const Transform3D &p_transform, Node3D *p_preview, bool p_asset_root);
	void _update_controls();
	void _collision_option(int p_option);
	void _show_option(int p_option);
	void _add_shape(int p_kind);
	void _show_convex_dialog();
	void _confirm_delete_collisions();
	void _delete_collisions();
	void _begin_decomposition();
	static void _decompose(void *p_userdata);
	void _add_shapes(StaticBody3D *p_root, const Vector<Ref<Shape3D>> &p_shapes, const Transform3D &p_transform, const String &p_label);

protected:
	void _notification(int p_what);

public:
	static WGodotMeshCollisionEditorPlugin *get_singleton() { return singleton; }
	void edit_asset(const String &p_path);
	Dictionary get_personal_state() const;
	void set_personal_state(const Dictionary &p_state);
	virtual String get_plugin_name() const override { return "MeshCollision"; }
	WGodotMeshCollisionEditorPlugin();
	~WGodotMeshCollisionEditorPlugin();
};
