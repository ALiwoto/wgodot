// wgodot-changes::file
#include "wgodot_mesh_audit_editor_plugin.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/wgodot_resizable_tree.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/scene/3d/wgodot_mesh_collision_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/tree.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/packed_scene.h"

const WGodotMeshAuditEditorPlugin::AssetDetails &WGodotMeshAuditEditorPlugin::_asset_details(const String &p_source) {
	if (const AssetDetails *cached = asset_cache.getptr(p_source)) {
		return *cached;
	}
	AssetDetails asset;
	asset.collision = WGodotMeshCollisionSettings::load(p_source);
	if (asset.collision.available) {
		const Ref<Resource> resource = ResourceLoader::load(p_source);
		const Ref<Mesh> mesh = resource;
		const Ref<PackedScene> scene = resource;
		if (mesh.is_valid()) {
			asset.mesh_keys[mesh->get_path()] = ".";
		} else if (scene.is_valid()) {
			// Imported subresource IDs are regenerated from the source path, and
			// mesh names can contain the filename. Neither survives a file rename.
			// Use a mesh's canonical node path within the source scene instead.
			// Inspect the saved state without instantiating any nodes or scripts.
			const Ref<SceneState> state = scene->get_state();
			for (int node = 0; node < state->get_node_count(); node++) {
				for (int property = 0; property < state->get_node_property_count(node); property++) {
					Ref<Mesh> source_mesh;
					const StringName name = state->get_node_property_name(node, property);
					if (name == SNAME("mesh")) {
						source_mesh = state->get_node_property_value(node, property);
					} else if (name == SNAME("multimesh")) {
						const Ref<MultiMesh> multi = state->get_node_property_value(node, property);
						if (multi.is_valid()) {
							source_mesh = multi->get_mesh();
						}
					}
					if (source_mesh.is_valid()) {
						const String path = source_mesh->get_path();
						const String key = String(state->get_node_path(node));
						const String *existing = asset.mesh_keys.getptr(path);
						if (!existing || key < *existing) {
							asset.mesh_keys[path] = key;
						}
					}
				}
			}
		}
	}
	return asset_cache.insert(p_source, asset)->value;
}

String WGodotMeshAuditEditorPlugin::_collision_decision(const Entry &p_entry) const {
	const WGodotMeshCollisionSettings &settings = asset_cache[p_entry.source].collision;
	if (settings.file_not_needed) {
		return TTR("Collision not needed for entire file");
	}
	if (!p_entry.mesh_key.is_empty() && settings.meshes_not_needed.has(p_entry.mesh_key)) {
		return TTR("Collision not needed for this mesh");
	}
	return String();
}

WGodotMeshAuditEditorPlugin::MeshDetails WGodotMeshAuditEditorPlugin::_mesh_details(const Ref<Mesh> &p_mesh) {
	const ObjectID id = p_mesh->get_instance_id();
	if (const MeshDetails *cached = mesh_cache.getptr(id)) {
		return *cached;
	}
	MeshDetails result;
	result.surfaces = p_mesh->get_surface_count();
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const int vertices = p_mesh->surface_get_array_len(surface);
		const int indices = p_mesh->surface_get_array_index_len(surface);
		const int elements = indices > 0 ? indices : vertices;
		result.vertices += vertices;
		const Mesh::PrimitiveType primitive = p_mesh->surface_get_primitive_type(surface);
		if (primitive == Mesh::PRIMITIVE_TRIANGLES) {
			result.triangles += elements / 3;
		} else if (primitive == Mesh::PRIMITIVE_TRIANGLE_STRIP) {
			result.triangles += MAX(0, elements - 2);
		}
	}
	mesh_cache[id] = result;
	return result;
}

WGodotMeshAuditEditorPlugin::CollisionDetails WGodotMeshAuditEditorPlugin::_collision_subtree(Node *p_node) {
	const ObjectID id = p_node->get_instance_id();
	if (const CollisionDetails *cached = collision_cache.getptr(id)) {
		return *cached;
	}
	CollisionDetails result;
	PhysicsBody3D *body = Object::cast_to<PhysicsBody3D>(p_node);
	if (body) {
		List<uint32_t> owners;
		body->get_shape_owners(&owners);
		for (uint32_t owner : owners) {
			for (int i = 0; i < body->shape_owner_get_shape_count(owner); i++) {
				if (body->is_shape_owner_disabled(owner)) {
					result.disabled_shapes++;
					continue;
				}
				const Ref<Shape3D> shape = body->shape_owner_get_shape(owner, i);
				result.shapes++;
				if (!result.description.is_empty()) {
					result.description += ", ";
				}
				result.description += shape->get_class();
				const Ref<ConcavePolygonShape3D> concave = shape;
				const Ref<ConvexPolygonShape3D> convex = shape;
				if (concave.is_valid()) {
					result.triangles += concave->get_faces().size() / 3;
				}
				if (convex.is_valid()) {
					result.convex_vertices += convex->get_points().size();
				}
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		const CollisionDetails child = _collision_subtree(p_node->get_child(i));
		result.shapes += child.shapes;
		result.disabled_shapes += child.disabled_shapes;
		result.triangles += child.triangles;
		result.convex_vertices += child.convex_vertices;
		if (!child.description.is_empty()) {
			result.description += (result.description.is_empty() ? "" : ", ") + child.description;
		}
	}
	collision_cache[id] = result;
	return result;
}

WGodotMeshAuditEditorPlugin::CollisionDetails WGodotMeshAuditEditorPlugin::_collision_details(Node *p_mesh, Node *p_scene) {
	// A containing physics body explicitly owns collision for its visuals.
	for (Node *node = p_mesh; node; node = node->get_parent()) {
		if (Object::cast_to<PhysicsBody3D>(node)) {
			return _collision_subtree(node);
		}
		if (node == p_scene) {
			break;
		}
	}
	// Imported models include their authored sidecar beneath the model root.
	// Do not climb into the surrounding map and attribute its floor to every mesh.
	for (Node *node = p_mesh; node; node = node->get_parent()) {
		if (node != p_scene && !node->get_scene_file_path().is_empty()) {
			return _collision_subtree(node);
		}
		if (node == p_scene) {
			break;
		}
	}
	return _collision_subtree(p_mesh);
}

void WGodotMeshAuditEditorPlugin::_collect(Node *p_node, Node *p_scene) {
	Ref<Mesh> mesh;
	AABB bounds;
	int instances = 1;
	if (MeshInstance3D *instance = Object::cast_to<MeshInstance3D>(p_node)) {
		mesh = instance->get_mesh();
		bounds = instance->get_aabb();
	} else if (MultiMeshInstance3D *instance = Object::cast_to<MultiMeshInstance3D>(p_node)) {
		const Ref<MultiMesh> multi = instance->get_multimesh();
		if (multi.is_valid()) {
			mesh = multi->get_mesh();
			bounds = instance->get_aabb();
			instances = multi->get_visible_instance_count();
			if (instances < 0) {
				instances = multi->get_instance_count();
			}
		}
	}
	if (mesh.is_valid()) {
		Entry entry;
		entry.node = p_node->get_instance_id();
		entry.path = String(p_scene->get_path_to(p_node));
		entry.mesh = _mesh_details(mesh);
		entry.instances = instances;
		entry.collision = _collision_details(p_node, p_scene);
		Node3D *spatial = Object::cast_to<Node3D>(p_node);
		entry.visible = spatial->is_visible_in_tree();
		entry.bounds = spatial->get_global_transform().xform(bounds).size;
		for (Node *node = p_node; node; node = node->get_parent()) {
			if (!node->get_scene_file_path().is_empty()) {
				entry.model_source = ResourceUID::ensure_path(node->get_scene_file_path());
				break;
			}
			if (node == p_scene) {
				break;
			}
		}
		entry.source = ResourceUID::ensure_path(mesh->get_path().get_slice("::", 0));
		if (entry.source.is_empty()) {
			entry.source = entry.model_source;
		}
		const AssetDetails &asset = _asset_details(entry.source);
		if (const String *key = asset.mesh_keys.getptr(mesh->get_path())) {
			entry.mesh_key = *key;
		}
		if (!entry.source.is_empty()) {
			if (!file_size_cache.has(entry.source)) {
				const Ref<FileAccess> file = FileAccess::open(entry.source, FileAccess::READ);
				file_size_cache[entry.source] = file.is_valid() ? int64_t(file->get_length()) : -1;
			}
			entry.file_bytes = file_size_cache[entry.source];
		}
		entries.push_back(entry);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect(p_node->get_child(i), p_scene);
	}
}

void WGodotMeshAuditEditorPlugin::_refresh() {
	entries.clear();
	mesh_cache.clear();
	collision_cache.clear();
	file_size_cache.clear();
	asset_cache.clear();
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (root) {
		_collect(root, root);
	}
	_filter();
}

void WGodotMeshAuditEditorPlugin::_filter() {
	results->clear();
	TreeItem *root = results->create_item();
	Vector<int> matches;
	const String query = search->get_text();
	int not_needed_count = 0;
	for (int i = 0; i < entries.size(); i++) {
		const Entry &entry = entries[i];
		if ((entry.collision.shapes > 0) != with_collision || (!include_hidden->is_pressed() && !entry.visible)) {
			continue;
		}
		if (!query.is_empty() && !entry.path.containsn(query) && !entry.source.containsn(query)) {
			continue;
		}
		if (!with_collision && !_collision_decision(entry).is_empty()) {
			not_needed_count++;
			if (!include_not_needed->is_pressed()) {
				continue;
			}
		}
		matches.push_back(i);
	}
	const auto compare = [this](int p_left, int p_right) {
		const Entry &a = entries[p_left];
		const Entry &b = entries[p_right];
		int order = 0;
		switch (sort_column) {
			case 1:
				order = a.mesh.triangles < b.mesh.triangles ? -1 : (a.mesh.triangles > b.mesh.triangles ? 1 : 0);
				break;
			case 2:
				order = a.mesh.vertices < b.mesh.vertices ? -1 : (a.mesh.vertices > b.mesh.vertices ? 1 : 0);
				break;
			case 3:
				order = a.mesh.surfaces - b.mesh.surfaces;
				break;
			case 4:
				order = a.instances - b.instances;
				break;
			case 5:
				order = a.collision.shapes - b.collision.shapes;
				break;
			case 6:
				order = a.file_bytes < b.file_bytes ? -1 : (a.file_bytes > b.file_bytes ? 1 : 0);
				break;
		}
		if (order == 0) {
			order = a.path.naturalnocasecmp_to(b.path);
		}
		return sort_descending ? order > 0 : order < 0;
	};
	matches.sort_custom<decltype(compare)>(compare);
	for (int index : matches) {
		const Entry &entry = entries[index];
		TreeItem *item = results->create_item(root);
		item->set_metadata(0, index);
		item->set_text(0, entry.path);
		item->set_text(1, itos(entry.mesh.triangles));
		item->set_text(2, itos(entry.mesh.vertices));
		item->set_text(3, itos(entry.mesh.surfaces));
		item->set_text(4, itos(entry.instances));
		item->set_text(5, itos(entry.collision.shapes));
		item->set_text(6, entry.file_bytes < 0 ? TTR("Unsaved") : String::humanize_size(entry.file_bytes));
		item->set_tooltip_text(0, entry.source + (entry.visible ? "" : "\n" + TTR("Hidden in the scene tree")));
		const String decision = _collision_decision(entry);
		if (!decision.is_empty()) {
			item->set_text(0, entry.path + " [" + TTR("Collision Not Needed") + "]");
			item->set_tooltip_text(0, item->get_tooltip_text(0) + "\n" + decision);
		}
		item->set_tooltip_text(5, entry.collision.description);
		item->set_tooltip_text(6, entry.source + "\n" + TTR("Size of the whole source file; shared across its instances. Embedded textures are included."));
	}
	summary->set_text(vformat(TTR("%d matching mesh nodes / %d total in the open scene. Triangle counts are LOD0 per mesh, before instancing."), matches.size(), entries.size()));
	if (not_needed_count > 0) {
		summary->set_text(summary->get_text() + "\n" + vformat(TTR("%d matching nodes marked Collision Not Needed. Use Show Not Needed to review or clear these choices."), not_needed_count));
	}
	_selection_changed();
}

void WGodotMeshAuditEditorPlugin::_search_changed(const String &p_text) {
	_filter();
}

void WGodotMeshAuditEditorPlugin::_hidden_changed(bool p_pressed) {
	_filter();
}

void WGodotMeshAuditEditorPlugin::_item_mouse_selected(const Vector2 &p_position, MouseButton p_button) {
	const Entry *entry = _selected();
	if (p_button != MouseButton::RIGHT || !entry) {
		return;
	}
	const WGodotMeshCollisionSettings &settings = asset_cache[entry->source].collision;
	collision_menu->set_item_checked(NOT_NEEDED_FILE, settings.file_not_needed);
	collision_menu->set_item_checked(NOT_NEEDED_MESH, settings.meshes_not_needed.has(entry->mesh_key));
	collision_menu->set_item_disabled(NOT_NEEDED_FILE, !settings.available);
	collision_menu->set_item_disabled(NOT_NEEDED_MESH, !settings.available || entry->mesh_key.is_empty());
	collision_menu->set_item_tooltip(NOT_NEEDED_FILE, settings.available ? entry->source + "\n" + TTR("Applies to every mesh and instance of this source. Click again to clear.") : TTR("Requires a saved imported asset with an .import file."));
	collision_menu->set_item_tooltip(NOT_NEEDED_MESH, entry->mesh_key.is_empty() ? TTR("This mesh has no identity in an imported source asset.") : entry->mesh_key + "\n" + TTR("Applies to this mesh in every instance of the source. Click again to clear."));
	context_menu->set_position(results->get_screen_position() + p_position);
	context_menu->reset_size();
	context_menu->popup();
}

void WGodotMeshAuditEditorPlugin::_collision_choice(int p_choice) {
	const Entry *entry = _selected();
	if (!entry) {
		return;
	}
	const String source = entry->source;
	const String key = p_choice == NOT_NEEDED_FILE ? String() : entry->mesh_key;
	const WGodotMeshCollisionSettings settings = WGodotMeshCollisionSettings::load(source);
	const bool was_not_needed = p_choice == NOT_NEEDED_FILE ? settings.file_not_needed : settings.meshes_not_needed.has(key);
	const Error error = EditorFileSystem::get_singleton()->set_mesh_collision_not_needed(source, key, !was_not_needed);
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save Collision Not Needed for %s: %s"), source, error_names[error]));
		return;
	}
	asset_cache[source].collision = WGodotMeshCollisionSettings::load(source);
	_filter();
}

void WGodotMeshAuditEditorPlugin::_sort(int p_column, int p_button) {
	if (p_button != int(MouseButton::LEFT)) {
		return;
	}
	sort_descending = sort_column == p_column ? !sort_descending : p_column != 0;
	sort_column = p_column;
	_update_sort_headers();
	_filter();
}

void WGodotMeshAuditEditorPlugin::_update_sort_headers() {
	const String titles[] = { TTR("Node"), TTR("Triangles"), TTR("Vertices"), TTR("Surfaces"), TTR("Instances"), TTR("Shapes"), TTR("Source Size") };
	for (int column = 0; column < results->get_columns(); column++) {
		String title = titles[column];
		String tooltip = TTR("Click to sort. Drag a column border to resize.");
		if (column == sort_column) {
			title += sort_descending ? U" \u25BC" : U" \u25B2";
			tooltip = sort_descending ? TTR("Sorted descending. Click to reverse. Drag a column border to resize.") : TTR("Sorted ascending. Click to reverse. Drag a column border to resize.");
		}
		results->set_column_title(column, title);
		results->set_column_title_tooltip_text(column, tooltip);
	}
}

const WGodotMeshAuditEditorPlugin::Entry *WGodotMeshAuditEditorPlugin::_selected() const {
	TreeItem *item = results->get_selected();
	return item ? &entries[int(item->get_metadata(0))] : nullptr;
}

void WGodotMeshAuditEditorPlugin::_selection_changed() {
	const Entry *entry = _selected();
	frame_button->set_disabled(!entry);
	asset_button->set_disabled(!entry || entry->source.is_empty());
	collision_button->set_disabled(!entry || !FileAccess::exists(entry->model_source + ".import") || ResourceLoader::get_resource_type(entry->model_source) != "PackedScene");
	if (!entry) {
		details->set_text(TTR("Select a mesh for details. Double-click to select and frame it in 3D."));
		return;
	}
	details->set_text(vformat(TTR("Source: %s\nWorld bounds: %s m | Collision triangles: %d | Convex vertices: %d | Disabled shapes: %d"),
			entry->source.is_empty() ? TTR("Built-in / unsaved") : entry->source,
			entry->bounds, entry->collision.triangles, entry->collision.convex_vertices, entry->collision.disabled_shapes));
	const String decision = _collision_decision(*entry);
	if (!decision.is_empty()) {
		details->set_text(details->get_text() + "\n" + decision + ". " + TTR("Editor annotation; existing physics is unchanged."));
	}
}

void WGodotMeshAuditEditorPlugin::_frame_selected() {
	const Entry *entry = _selected();
	if (!entry) {
		return;
	}
	Node *node = Object::cast_to<Node>(ObjectDB::get_instance(entry->node));
	if (!node || !node->is_inside_tree()) {
		_refresh();
		return;
	}
	dialog->hide();
	EditorInterface *editor = EditorInterface::get_singleton();
	editor->set_main_screen_editor("3D");
	editor->get_selection()->clear();
	editor->get_selection()->add_node(node);
	editor->edit_node(node);
	Node3DEditor::get_singleton()->get_editor_viewport(0)->focus_selection();
}

void WGodotMeshAuditEditorPlugin::_show_asset() {
	const Entry *entry = _selected();
	if (entry && !entry->source.is_empty()) {
		EditorInterface::get_singleton()->select_file(entry->source);
		dialog->hide();
	}
}

void WGodotMeshAuditEditorPlugin::_edit_collision() {
	const Entry *entry = _selected();
	if (entry) {
		const String source = entry->model_source;
		dialog->hide();
		WGodotMeshCollisionEditorPlugin::get_singleton()->edit_asset(source);
	}
}

void WGodotMeshAuditEditorPlugin::_scene_changed(Node *p_root) {
	if (dialog->is_visible()) {
		_refresh();
	}
}

void WGodotMeshAuditEditorPlugin::_open(bool p_with_collision) {
	with_collision = p_with_collision;
	include_not_needed->set_visible(!with_collision);
	dialog->set_title(with_collision ? TTR("3D Meshes with Collisions") : TTR("3D Meshes without Collisions"));
	// Hidden containers have not assigned these labels a width on first opening.
	// Seed wrapping before the dialog measures its content's minimum height.
	for (Label *label : { summary, details, scope }) {
		label->set_size(Size2(dialog->get_min_size().x, 0));
		label->update_minimum_size();
	}
	search->clear();
	_refresh();
	dialog->popup_centered_clamped(Size2(1150, 620) * EDSCALE, 0.9);
	search->grab_focus();
}

void WGodotMeshAuditEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		connect("scene_changed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_scene_changed));
		EditorCommandPalette::get_singleton()->add_command(TTR("List 3D Meshes with collisions"), "mesh/list_with_collisions", callable_mp(this, &WGodotMeshAuditEditorPlugin::_open).bind(true));
		EditorCommandPalette::get_singleton()->add_command(TTR("List 3D Meshes without collisions"), "mesh/list_without_collisions", callable_mp(this, &WGodotMeshAuditEditorPlugin::_open).bind(false));
	}
}

WGodotMeshAuditEditorPlugin::WGodotMeshAuditEditorPlugin() {
	dialog = memnew(AcceptDialog);
	dialog->set_name("MeshCollisionList");
	dialog->set_min_size(Size2i(760, 420) * EDSCALE);
	dialog->set_ok_button_text(TTR("Close"));
	dialog->set_flag(Window::FLAG_RESIZE_DISABLED, false);
	add_child(dialog);
	VBoxContainer *layout = memnew(VBoxContainer);
	dialog->add_child(layout);
	HBoxContainer *filters = memnew(HBoxContainer);
	layout->add_child(filters);
	search = memnew(LineEdit);
	search->set_placeholder(TTR("Search node or source path"));
	search->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	filters->add_child(search);
	search->connect("text_changed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_search_changed));
	include_hidden = memnew(CheckBox);
	include_hidden->set_text(TTR("Include Hidden"));
	include_hidden->set_pressed(true);
	filters->add_child(include_hidden);
	include_hidden->connect("toggled", callable_mp(this, &WGodotMeshAuditEditorPlugin::_hidden_changed));
	include_not_needed = memnew(CheckBox);
	include_not_needed->set_text(TTR("Show Not Needed"));
	include_not_needed->set_tooltip_text(TTR("Show meshes you marked as not needing collision. Right-click and uncheck a choice to clear it."));
	filters->add_child(include_not_needed);
	include_not_needed->connect("toggled", callable_mp(this, &WGodotMeshAuditEditorPlugin::_hidden_changed));
	Button *refresh = memnew(Button);
	refresh->set_text(TTR("Refresh"));
	filters->add_child(refresh);
	refresh->connect("pressed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_refresh));
	results = memnew(WGodotResizableTree);
	results->set_columns(7);
	results->set_hide_root(true);
	results->set_select_mode(Tree::SELECT_ROW);
	results->set_allow_rmb_select(true);
	results->set_column_titles_visible(true);
	results->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	for (int i = 0; i < 7; i++) {
		results->set_column_expand(i, i == 0);
		results->set_column_clip_content(i, true);
		results->set_column_custom_minimum_width(i, (i == 0 ? 270 : 100) * EDSCALE);
	}
	_update_sort_headers();
	layout->add_child(results);
	results->connect("item_selected", callable_mp(this, &WGodotMeshAuditEditorPlugin::_selection_changed));
	results->connect("item_activated", callable_mp(this, &WGodotMeshAuditEditorPlugin::_frame_selected));
	results->connect("column_title_clicked", callable_mp(this, &WGodotMeshAuditEditorPlugin::_sort));
	results->connect("item_mouse_selected", callable_mp(this, &WGodotMeshAuditEditorPlugin::_item_mouse_selected));
	context_menu = memnew(PopupMenu);
	dialog->add_child(context_menu);
	collision_menu = memnew(PopupMenu);
	collision_menu->add_check_item(TTR("Not Needed for entire file"), NOT_NEEDED_FILE);
	collision_menu->add_check_item(TTR("Not Needed for this mesh"), NOT_NEEDED_MESH);
	collision_menu->connect("id_pressed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_collision_choice));
	context_menu->add_submenu_node_item(TTR("Collision Not Needed"), collision_menu);
	summary = memnew(Label);
	summary->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(summary);
	details = memnew(Label);
	details->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(details);
	scope = memnew(Label);
	scope->set_text(TTR("Collision means enabled physics shapes on the mesh, its containing body, or its instanced model/prop. Separate map-wide colliders are not matched by overlap. Area triggers do not count."));
	scope->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(scope);
	HBoxContainer *actions = memnew(HBoxContainer);
	layout->add_child(actions);
	frame_button = memnew(Button);
	frame_button->set_text(TTR("Select and Frame"));
	actions->add_child(frame_button);
	frame_button->connect("pressed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_frame_selected));
	asset_button = memnew(Button);
	asset_button->set_text(TTR("Show Source File"));
	actions->add_child(asset_button);
	asset_button->connect("pressed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_show_asset));
	collision_button = memnew(Button);
	collision_button->set_text(TTR("Edit Collision..."));
	actions->add_child(collision_button);
	collision_button->connect("pressed", callable_mp(this, &WGodotMeshAuditEditorPlugin::_edit_collision));
}
