// wgodot-changes::file
#pragma once

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/resources/mesh.h"

class HBoxContainer;
class MenuButton;
class Button;
class Label;
class SpinBox;
class ConfirmationDialog;
class StaticBody3D;
class Shape3D;
class Node3D;

class WGodotMeshCollisionEditorPlugin : public EditorPlugin {
	GDCLASS(WGodotMeshCollisionEditorPlugin, EditorPlugin);

	enum ShapeKind {
		BOX,
		SPHERE,
		CAPSULE,
		CYLINDER,
		CONVEX,
	};
	static WGodotMeshCollisionEditorPlugin *singleton;
	ObjectID edited_root;
	ObjectID preview_root;
	AABB mesh_bounds;
	Vector<Vector3> mesh_faces;
	HBoxContainer *toolbar = nullptr;
	MenuButton *add_shape = nullptr;
	Button *auto_convex = nullptr;
	Button *show_mesh = nullptr;
	Label *status = nullptr;
	ConfirmationDialog *convex_dialog = nullptr;
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
	void _show_mesh(bool p_visible);
	void _add_shape(int p_kind);
	void _show_convex_dialog();
	void _begin_decomposition();
	static void _decompose(void *p_userdata);
	void _add_shapes(StaticBody3D *p_root, const Vector<Ref<Shape3D>> &p_shapes, const Transform3D &p_transform, const String &p_label);

protected:
	void _notification(int p_what);

public:
	static WGodotMeshCollisionEditorPlugin *get_singleton() { return singleton; }
	void edit_asset(const String &p_path);
	virtual String get_plugin_name() const override { return "MeshCollision"; }
	WGodotMeshCollisionEditorPlugin();
	~WGodotMeshCollisionEditorPlugin();
};
