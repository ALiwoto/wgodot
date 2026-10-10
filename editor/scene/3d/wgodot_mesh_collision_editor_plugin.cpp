// wgodot-changes::file
#include "wgodot_mesh_collision_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/math/quick_hull.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/import/3d/wgodot_mesh_collision.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/packed_scene.h"

WGodotMeshCollisionEditorPlugin *WGodotMeshCollisionEditorPlugin::singleton = nullptr;

void WGodotMeshCollisionEditorPlugin::edit_asset(const String &p_path) {
	const String source = ResourceUID::ensure_path(p_path.get_slice("::", 0));
	if (!FileAccess::exists(source + ".import") || ResourceLoader::get_resource_type(source) != "PackedScene") {
		EditorNode::get_singleton()->show_warning(TTR("Select an imported 3D model to edit its collision sidecar."));
		return;
	}
	const String path = WGodotMeshCollision::sidecar_path(source);
	if (!FileAccess::exists(path)) {
		StaticBody3D *body = nullptr;
		const String imported = WGodotMeshCollision::imported_path(source);
		if (FileAccess::exists(imported)) {
			Ref<PackedScene> original = ResourceLoader::load(imported);
			ERR_FAIL_COND_MSG(original.is_null(), "Cannot load imported collision: " + imported);
			Node *root = original->instantiate();
			body = Object::cast_to<StaticBody3D>(root);
			if (!body) {
				memdelete(root);
				ERR_FAIL_MSG("Imported collision must have a StaticBody3D root: " + imported);
			}
			body->set_scene_file_path(String());
			// The new authored sidecar owns its shapes. Editing it must neither
			// mutate the imported defaults nor depend on them in exported packages.
			for (int i = 0; i < body->get_child_count(); i++) {
				CollisionShape3D *shape = Object::cast_to<CollisionShape3D>(body->get_child(i));
				if (shape && shape->get_shape().is_valid()) {
					shape->set_shape(shape->get_shape()->duplicate(true));
				}
			}
		} else {
			body = memnew(StaticBody3D);
			body->set_collision_layer(1);
			body->set_collision_mask(0);
		}
		if (body->get_collision_layer() == 0) {
			// Authoring a custom collider overrides the asset's NoCollision default.
			// Explicit placement overrides are still applied by the containing scene.
			body->set_collision_layer(1);
			body->set_process_mode(Node::PROCESS_MODE_INHERIT);
		}
		body->set_name("MeshCollision");
		const ResourceUID::ID uid = ResourceLoader::get_resource_uid(source);
		body->set_meta(WGodotMeshCollision::SOURCE_META, uid == ResourceUID::INVALID_ID ? source : ResourceUID::get_singleton()->id_to_text(uid));
		Ref<PackedScene> scene;
		scene.instantiate();
		Error error = scene->pack(body);
		memdelete(body);
		if (error == OK) {
			error = ResourceSaver::save(scene, path);
		}
		if (error != OK) {
			EditorNode::get_singleton()->show_warning(vformat(TTR("Cannot save collision sidecar: %s"), path));
			return;
		}
		EditorFileSystem::get_singleton()->update_file(path);
	}
	EditorNode::get_singleton()->open_scene(path);
	EditorInterface::get_singleton()->set_main_screen_editor("3D");
}

void WGodotMeshCollisionEditorPlugin::_clear_preview() {
	Node *preview = Object::cast_to<Node>(ObjectDB::get_instance(preview_root));
	if (preview) {
		memdelete(preview);
	}
	preview_root = ObjectID();
	mesh_faces.clear();
	mesh_bounds = AABB();
}

void WGodotMeshCollisionEditorPlugin::_collect_meshes(Node *p_node, const Transform3D &p_transform, Node3D *p_preview, bool p_asset_root) {
	Transform3D transform = p_transform;
	Node3D *spatial = Object::cast_to<Node3D>(p_node);
	if (spatial && !p_asset_root) {
		transform *= spatial->get_transform();
	}
	MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(p_node);
	if (mesh_node && mesh_node->get_mesh().is_valid()) {
		MeshInstance3D *visual = memnew(MeshInstance3D);
		visual->set_mesh(mesh_node->get_mesh());
		visual->set_transform(transform);
		visual->set_material_override(mesh_node->get_material_override());
		for (int i = 0; i < mesh_node->get_surface_override_material_count(); i++) {
			visual->set_surface_override_material(i, mesh_node->get_surface_override_material(i));
		}
		visual->set_meta("_edit_lock_", true);
		p_preview->add_child(visual);
		const Vector<Face3> faces = mesh_node->get_mesh()->get_faces();
		for (const Face3 &face : faces) {
			for (const Vector3 &vertex : face.vertex) {
				const Vector3 point = transform.xform(vertex);
				if (mesh_faces.is_empty()) {
					mesh_bounds = AABB(point, Vector3());
				} else {
					mesh_bounds.expand_to(point);
				}
				mesh_faces.push_back(point);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_meshes(p_node->get_child(i), transform, p_preview, false);
	}
}

void WGodotMeshCollisionEditorPlugin::_scene_changed(Node *p_root) {
	_clear_preview();
	edited_root = ObjectID();
	toolbar->hide();
	convex_dialog->hide();
	delete_dialog->hide();
	deletion_root = ObjectID();
	if (!decomposition_thread.is_started()) {
		status->hide();
	}
	StaticBody3D *body = Object::cast_to<StaticBody3D>(p_root);
	if (!body || !body->has_meta(WGodotMeshCollision::SOURCE_META)) {
		return;
	}
	const String source = ResourceUID::ensure_path(body->get_meta(WGodotMeshCollision::SOURCE_META));
	Ref<PackedScene> asset = ResourceLoader::load(source);
	if (asset.is_null()) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Cannot load the collision editing reference: %s"), source));
		return;
	}
	Node *model = asset->instantiate();
	if (!model) {
		return;
	}
	Node3D *preview = memnew(Node3D);
	preview->set_name("MeshReference");
	preview->set_as_top_level(true);
	// No owner: the visible reference and its resources never enter the saved sidecar.
	body->add_child(preview, false, Node::INTERNAL_MODE_BACK);
	preview_root = preview->get_instance_id();
	_collect_meshes(model, Transform3D(), preview, true);
	memdelete(model);
	edited_root = body->get_instance_id();
	show_menu->get_popup()->set_item_checked(show_menu->get_popup()->get_item_index(SHOW_MESH), true);
	_update_controls();
	toolbar->show();
	// Frame only on first opening; preserve Godot's restored view on later visits.
	const int scene_index = EditorNode::get_editor_data().get_edited_scene();
	const Dictionary editor_states = EditorNode::get_editor_data().get_scene_editor_states(scene_index);
	if (!editor_states.has("3D")) {
		Dictionary view;
		view["position"] = mesh_bounds.get_center();
		view["distance"] = MAX(mesh_bounds.get_longest_axis_size() * 2.0, 1.0);
		Node3DEditor::get_singleton()->get_editor_viewport(0)->set_state(view);
	}
}

Dictionary WGodotMeshCollisionEditorPlugin::get_personal_state() const {
	Dictionary state;
	Node3D *preview = Object::cast_to<Node3D>(ObjectDB::get_instance(preview_root));
	if (preview) {
		state["show_mesh"] = preview->is_visible();
		state["hull_count"] = hull_count->get_value();
		state["hull_vertices"] = hull_vertices->get_value();
		state["precision"] = precision->get_value();
	}
	return state;
}

void WGodotMeshCollisionEditorPlugin::set_personal_state(const Dictionary &p_state) {
	Node3D *preview = Object::cast_to<Node3D>(ObjectDB::get_instance(preview_root));
	if (!preview) {
		return;
	}
	if (p_state.has("show_mesh")) {
		const bool visible = p_state["show_mesh"];
		preview->set_visible(visible);
		show_menu->get_popup()->set_item_checked(show_menu->get_popup()->get_item_index(SHOW_MESH), visible);
	}
	hull_count->set_value(p_state.get("hull_count", 8));
	hull_vertices->set_value(p_state.get("hull_vertices", 16));
	precision->set_value(p_state.get("precision", 100000));
}

void WGodotMeshCollisionEditorPlugin::_update_controls() {
	PopupMenu *popup = collision_menu->get_popup();
	for (int option : { BOX, SPHERE, CAPSULE, CYLINDER, CONVEX }) {
		popup->set_item_disabled(popup->get_item_index(option), mesh_faces.is_empty());
	}
	const bool convex_available = !mesh_faces.is_empty() && Mesh::convex_decomposition_function;
	const bool generating = decomposition_thread.is_started();
	popup->set_item_disabled(popup->get_item_index(DELETE_CUSTOM_COLLISIONS), generating);
	// Keep the command available during generation so its progress can be reopened.
	popup->set_item_disabled(popup->get_item_index(AUTO_CONVEX), !convex_available);
	convex_dialog->get_ok_button()->set_disabled(!convex_available || generating);
	convex_dialog->set_ok_button_text(generating ? TTR("Generating...") : TTR("Generate"));
	convex_dialog->get_cancel_button()->set_tooltip_text(generating ? TTR("Close this window. Generation continues in the background.") : String());
	for (SpinBox *field : { hull_count, hull_vertices, precision }) {
		field->set_editable(!generating);
	}
}

void WGodotMeshCollisionEditorPlugin::_collision_option(int p_option) {
	if (p_option == AUTO_CONVEX) {
		_show_convex_dialog();
	} else if (p_option == DELETE_CUSTOM_COLLISIONS) {
		_confirm_delete_collisions();
	} else {
		_add_shape(p_option);
	}
}

void WGodotMeshCollisionEditorPlugin::_confirm_delete_collisions() {
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(edited_root));
	if (!root || decomposition_thread.is_started()) {
		return;
	}
	deletion_source = ResourceUID::ensure_path(root->get_meta(WGodotMeshCollision::SOURCE_META));
	deletion_path = WGodotMeshCollision::sidecar_path(deletion_source);
	if (root->get_scene_file_path() != deletion_path) {
		EditorNode::get_singleton()->show_warning(TTR("Open the model's collision sidecar before deleting its custom collisions."));
		return;
	}
	deletion_root = edited_root;
	delete_dialog->set_text(vformat(TTR("This action will delete %s file. Continue?"), deletion_path));
	delete_dialog->popup_centered();
}

void WGodotMeshCollisionEditorPlugin::_delete_collisions() {
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(deletion_root));
	if (!root || deletion_root != edited_root || root != EditorNode::get_singleton()->get_edited_scene()) {
		return;
	}
	const String path = deletion_path;
	const String source = deletion_source;
	const Error error = OS::get_singleton()->move_to_trash(ProjectSettings::get_singleton()->globalize_path(path));
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Cannot delete collision sidecar: %s (%s)"), path, error_names[error]));
		return;
	}
	if (ResourceCache::has(path)) {
		ResourceCache::get_ref(path)->set_path(String());
	}
	// The confirmation also discards this sidecar's unsaved shape edits. Close it
	// so a later Save All cannot recreate the file that was just removed.
	EditorNode::get_singleton()->close_scene();
	EditorFileSystem::get_singleton()->update_file(path);
	callable_mp(this, &WGodotMeshCollisionEditorPlugin::_reimport_asset).call_deferred(source);
}

void WGodotMeshCollisionEditorPlugin::_show_option(int p_option) {
	if (p_option != SHOW_MESH) {
		return;
	}
	Node3D *preview = Object::cast_to<Node3D>(ObjectDB::get_instance(preview_root));
	if (preview) {
		const bool visible = !preview->is_visible();
		preview->set_visible(visible);
		show_menu->get_popup()->set_item_checked(show_menu->get_popup()->get_item_index(SHOW_MESH), visible);
	}
}

void WGodotMeshCollisionEditorPlugin::_add_shapes(StaticBody3D *p_root, const Vector<Ref<Shape3D>> &p_shapes, const Transform3D &p_transform, const String &p_label) {
	EditorUndoRedoManager *undo = EditorUndoRedoManager::get_singleton();
	undo->create_action(p_label, UndoRedo::MERGE_DISABLE, p_root);
	Vector<CollisionShape3D *> added;
	for (const Ref<Shape3D> &shape : p_shapes) {
		CollisionShape3D *node = memnew(CollisionShape3D);
		node->set_name(shape->get_class().trim_suffix("Shape3D"));
		node->set_shape(shape);
		node->set_transform(p_root->get_global_transform().affine_inverse() * p_transform);
		undo->add_do_method(p_root, "add_child", node, true);
		undo->add_do_method(node, "set_owner", p_root);
		undo->add_do_method(Node3DEditor::get_singleton(), "_request_gizmo", node);
		undo->add_do_reference(node);
		undo->add_undo_method(p_root, "remove_child", node);
		added.push_back(node);
	}
	undo->commit_action();
	EditorSelection *selection = EditorNode::get_singleton()->get_editor_selection();
	selection->clear();
	for (CollisionShape3D *node : added) {
		selection->add_node(node);
	}
	if (added.size() == 1) {
		EditorInterface::get_singleton()->edit_node(added[0]);
	}
}

void WGodotMeshCollisionEditorPlugin::_add_shape(int p_kind) {
	StaticBody3D *body = Object::cast_to<StaticBody3D>(ObjectDB::get_instance(edited_root));
	if (!body || mesh_faces.is_empty()) {
		return;
	}
	const Vector3 size = mesh_bounds.size.max(Vector3(0.01, 0.01, 0.01));
	Transform3D transform(Basis(), mesh_bounds.get_center());
	Ref<Shape3D> shape;
	switch (p_kind) {
		case BOX: {
			Ref<BoxShape3D> box;
			box.instantiate();
			box->set_size(size);
			shape = box;
		} break;
		case SPHERE: {
			Ref<SphereShape3D> sphere;
			sphere.instantiate();
			sphere->set_radius(size.length() * 0.5);
			shape = sphere;
		} break;
		case CAPSULE: {
			Ref<CapsuleShape3D> capsule;
			capsule.instantiate();
			const real_t radius = MAX(size.x, size.z) * 0.5;
			capsule->set_radius(radius);
			capsule->set_height(MAX(size.y, radius * 2.0));
			shape = capsule;
		} break;
		case CYLINDER: {
			Ref<CylinderShape3D> cylinder;
			cylinder.instantiate();
			cylinder->set_radius(MAX(size.x, size.z) * 0.5);
			cylinder->set_height(size.y);
			shape = cylinder;
		} break;
		case CONVEX: {
			Geometry3D::MeshData hull;
			if (QuickHull::build(mesh_faces, hull) != OK) {
				EditorNode::get_singleton()->show_warning(TTR("The mesh did not produce a convex volume."));
				return;
			}
			Ref<ConvexPolygonShape3D> convex;
			convex.instantiate();
			Vector<Vector3> points;
			points.resize(hull.vertices.size());
			for (uint32_t i = 0; i < hull.vertices.size(); i++) {
				points.write[i] = hull.vertices[i];
			}
			convex->set_points(points);
			shape = convex;
			transform = Transform3D();
		} break;
	}
	if (shape.is_valid()) {
		Vector<Ref<Shape3D>> shapes;
		shapes.push_back(shape);
		_add_shapes(body, shapes, transform, TTR("Add Mesh Collision Shape"));
	}
}

void WGodotMeshCollisionEditorPlugin::_show_convex_dialog() {
	_update_controls();
	convex_dialog->popup_centered(Size2(420, 0) * EDSCALE);
}

void WGodotMeshCollisionEditorPlugin::_begin_decomposition() {
	if (decomposition_thread.is_started() || mesh_faces.is_empty() || !Mesh::convex_decomposition_function) {
		return;
	}
	decomposition_root = edited_root;
	decomposition_vertices.resize(mesh_faces.size() * 3);
	decomposition_indices.resize(mesh_faces.size());
	for (int i = 0; i < mesh_faces.size(); i++) {
		for (int axis = 0; axis < 3; axis++) {
			decomposition_vertices.write[i * 3 + axis] = mesh_faces[i][axis];
		}
		decomposition_indices.write[i] = i;
	}
	decomposition_settings.instantiate();
	decomposition_settings->set_max_convex_hulls(uint32_t(hull_count->get_value()));
	decomposition_settings->set_max_num_vertices_per_convex_hull(uint32_t(hull_vertices->get_value()));
	decomposition_settings->set_resolution(uint32_t(precision->get_value()));
	decomposition_settings->set_max_concavity(0.001);
	decomposition_done.clear();
	status->set_text(TTR("Generating convex shapes..."));
	status->show();
	decomposition_thread.start(&_decompose, this);
	_update_controls();
	if (!decomposition_thread.is_started()) {
		status->set_text(TTR("Could not start convex decomposition."));
		return;
	}
	set_process(true);
}

void WGodotMeshCollisionEditorPlugin::_decompose(void *p_userdata) {
	WGodotMeshCollisionEditorPlugin *editor = static_cast<WGodotMeshCollisionEditorPlugin *>(p_userdata);
	editor->decomposition_hulls = Mesh::convex_decomposition_function(
			editor->decomposition_vertices.ptr(), editor->decomposition_vertices.size() / 3,
			editor->decomposition_indices.ptr(), editor->decomposition_indices.size() / 3,
			editor->decomposition_settings, nullptr);
	editor->decomposition_done.set();
}

void WGodotMeshCollisionEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		connect("scene_changed", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_scene_changed));
		connect("scene_saved", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_scene_saved));
	}
	if (p_what != NOTIFICATION_PROCESS || !decomposition_done.is_set()) {
		return;
	}
	decomposition_thread.wait_to_finish();
	set_process(false);
	StaticBody3D *body = Object::cast_to<StaticBody3D>(ObjectDB::get_instance(decomposition_root));
	if (body && decomposition_root == edited_root) {
		Vector<Ref<Shape3D>> shapes;
		for (const Vector<Vector3> &points : decomposition_hulls) {
			Ref<ConvexPolygonShape3D> shape;
			shape.instantiate();
			shape->set_points(points);
			shapes.push_back(shape);
		}
		if (!shapes.is_empty()) {
			_add_shapes(body, shapes, Transform3D(), TTR("Add Auto Convex Collision"));
		}
		status->set_text(vformat(TTR("%d convex shapes added"), shapes.size()));
		if (shapes.is_empty()) {
			EditorNode::get_singleton()->show_warning(TTR("Convex decomposition produced no shapes. Inspect the model and decomposition settings."));
		}
	} else {
		status->set_text(TTR("Asset changed; generated shapes were discarded."));
	}
	decomposition_hulls.clear();
	decomposition_vertices.clear();
	decomposition_indices.clear();
	decomposition_settings.unref();
	decomposition_done.clear();
	_update_controls();
}

void WGodotMeshCollisionEditorPlugin::_scene_saved(const String &p_path) {
	if (!p_path.ends_with("_collision.tscn")) {
		return;
	}
	Ref<PackedScene> scene = ResourceLoader::load(p_path);
	if (scene.is_null()) {
		return;
	}
	const Ref<SceneState> state = scene->get_state();
	for (int i = 0; i < state->get_node_property_count(0); i++) {
		if (state->get_node_property_name(0, i) == String("metadata/") + WGodotMeshCollision::SOURCE_META) {
			const String source = ResourceUID::ensure_path(state->get_node_property_value(0, i));
			callable_mp(this, &WGodotMeshCollisionEditorPlugin::_reimport_asset).call_deferred(source);
			return;
		}
	}
}

void WGodotMeshCollisionEditorPlugin::_reimport_asset(const String &p_source) {
	Vector<String> sources;
	sources.push_back(p_source);
	EditorFileSystem::get_singleton()->reimport_files(sources);
}

WGodotMeshCollisionEditorPlugin::WGodotMeshCollisionEditorPlugin() {
	singleton = this;
	toolbar = memnew(HBoxContainer);
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
	toolbar->hide();
	// Group authoring commands by task; viewport visibility has its own menu.
	collision_menu = memnew(MenuButton);
	collision_menu->set_text(TTR("Collision"));
	collision_menu->set_tooltip_text(TTR("Create collision shapes. Select a shape to edit its transform and dimensions in the Inspector or with the 3D gizmos. Ctrl+S saves."));
	show_menu = memnew(MenuButton);
	show_menu->set_text(TTR("Show"));
	show_menu->set_tooltip_text(TTR("Collision editing preview visibility."));
	for (MenuButton *menu : { collision_menu, show_menu }) {
		menu->set_flat(false);
		menu->set_theme_type_variation("FlatMenuButton");
		menu->set_switch_on_hover(true);
		toolbar->add_child(menu);
	}
	PopupMenu *collision_popup = collision_menu->get_popup();
	collision_popup->add_separator(TTR("Add Primitive"));
	collision_popup->add_item(TTR("Box"), BOX);
	collision_popup->add_item(TTR("Sphere"), SPHERE);
	collision_popup->add_item(TTR("Capsule"), CAPSULE);
	collision_popup->add_item(TTR("Cylinder"), CYLINDER);
	collision_popup->add_separator(TTR("Generate from Mesh"));
	collision_popup->add_item(TTR("Single Convex Hull"), CONVEX);
	collision_popup->add_item(TTR("Auto Convex..."), AUTO_CONVEX);
	collision_popup->add_separator();
	collision_popup->add_item(TTR("Delete Custom Collisions..."), DELETE_CUSTOM_COLLISIONS);
	collision_popup->set_item_tooltip(collision_popup->get_item_index(CONVEX), TTR("Create one convex shape enclosing the reference mesh."));
	collision_popup->set_item_tooltip(collision_popup->get_item_index(AUTO_CONVEX), TTR("Configure the hull count, vertices and precision before generating editable convex shapes."));
	collision_popup->connect("id_pressed", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_collision_option));
	PopupMenu *show_popup = show_menu->get_popup();
	show_popup->add_check_item(TTR("Reference Mesh"), SHOW_MESH);
	show_popup->set_item_tooltip(show_popup->get_item_index(SHOW_MESH), TTR("Show the original model while editing its collision shapes."));
	show_popup->set_hide_on_checkable_item_selection(false);
	show_popup->connect("id_pressed", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_show_option));

	delete_dialog = memnew(ConfirmationDialog);
	delete_dialog->set_title(TTR("Delete Custom Collisions"));
	delete_dialog->set_ok_button_text(TTR("Yes"));
	delete_dialog->set_cancel_button_text(TTR("No"));
	delete_dialog->connect("confirmed", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_delete_collisions));
	add_child(delete_dialog);

	convex_dialog = memnew(ConfirmationDialog);
	convex_dialog->set_title(TTR("Auto Convex Collision"));
	convex_dialog->set_ok_button_text(TTR("Generate"));
	convex_dialog->set_cancel_button_text(TTR("Close"));
	convex_dialog->set_hide_on_ok(false);
	add_child(convex_dialog);
	VBoxContainer *fields = memnew(VBoxContainer);
	convex_dialog->add_child(fields);
	GridContainer *parameters = memnew(GridContainer);
	parameters->set_columns(2);
	fields->add_child(parameters);
	auto field = [parameters](const String &p_label, double p_min, double p_max, double p_value) {
		Label *label = memnew(Label);
		label->set_text(p_label);
		parameters->add_child(label);
		SpinBox *spin = memnew(SpinBox);
		spin->set_accessibility_name(p_label);
		spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		spin->set_min(p_min);
		spin->set_max(p_max);
		spin->set_step(1);
		spin->set_value(p_value);
		parameters->add_child(spin);
		return spin;
	};
	hull_count = field(TTR("Maximum Hulls"), 1, 128, 8);
	hull_vertices = field(TTR("Maximum Vertices per Hull"), 4, 256, 16);
	precision = field(TTR("Voxel Resolution"), 10000, 1000000, 100000);
	Label *hint = memnew(Label);
	hint->set_text(TTR("Adds editable convex shapes. Existing shapes are preserved.\nFewer hulls and vertices reduce collision cost."));
	fields->add_child(hint);
	status = memnew(Label);
	status->hide();
	fields->add_child(status);
	convex_dialog->connect("confirmed", callable_mp(this, &WGodotMeshCollisionEditorPlugin::_begin_decomposition));
	set_process(false);
}

WGodotMeshCollisionEditorPlugin::~WGodotMeshCollisionEditorPlugin() {
	if (decomposition_thread.is_started()) {
		decomposition_thread.wait_to_finish();
	}
	_clear_preview();
	singleton = nullptr;
}
