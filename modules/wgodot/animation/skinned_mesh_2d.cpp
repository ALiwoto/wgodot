// wgodot-changes::file
#include "skinned_mesh_2d.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#define SKIN_SETTER(m_type, m_name) \
	void Skin2D::set_##m_name(const m_type &p_value) { \
		m_name = p_value; \
		emit_changed(); \
	}
SKIN_SETTER(PackedInt32Array, influence_offsets)
SKIN_SETTER(PackedInt32Array, bones)
SKIN_SETTER(PackedVector2Array, bind_positions)
SKIN_SETTER(PackedFloat32Array, weights)
SKIN_SETTER(PackedVector2Array, uvs)
SKIN_SETTER(PackedInt32Array, triangles)
#undef SKIN_SETTER

Error Skin2D::validate(int p_bone_count) const {
	ERR_FAIL_COND_V(influence_offsets.size() != uvs.size() + 1 || uvs.is_empty(), ERR_INVALID_DATA);
	ERR_FAIL_COND_V(bones.size() != bind_positions.size() || weights.size() != bones.size(), ERR_INVALID_DATA);
	ERR_FAIL_COND_V(influence_offsets[0] != 0 || influence_offsets[uvs.size()] != bones.size(), ERR_INVALID_DATA);
	for (int i = 0; i < uvs.size(); i++) {
		ERR_FAIL_COND_V(!uvs[i].is_finite() || influence_offsets[i] >= influence_offsets[i + 1], ERR_INVALID_DATA);
	}
	for (int i = 0; i < bones.size(); i++) {
		ERR_FAIL_COND_V(bones[i] < 0 || bones[i] >= p_bone_count || !bind_positions[i].is_finite() || !Math::is_finite(weights[i]), ERR_INVALID_DATA);
	}
	ERR_FAIL_COND_V(triangles.is_empty() || triangles.size() % 3 != 0, ERR_INVALID_DATA);
	for (int index : triangles) {
		ERR_FAIL_INDEX_V(index, uvs.size(), ERR_INVALID_DATA);
	}
	return OK;
}

void Skin2D::deform(const LocalVector<AffineBone2D *> &p_bones, const PackedVector2Array &p_deformation, PackedVector2Array &r_vertices) const {
	const int *offsets = influence_offsets.ptr();
	const int *indices = bones.ptr();
	const Vector2 *positions = bind_positions.ptr();
	const float *influence_weights = weights.ptr();
	const Vector2 *offset = p_deformation.is_empty() ? nullptr : p_deformation.ptr();
	r_vertices.resize(uvs.size());
	Vector2 *output = r_vertices.ptrw();
	for (int i = 0; i < uvs.size(); i++) {
		Vector2 position;
		for (int j = offsets[i]; j < offsets[i + 1]; j++) {
			Vector2 local = positions[j] + (offset ? offset[j] : Vector2());
			position += p_bones[indices[j]]->pose_transform().xform(local) * influence_weights[j];
		}
		output[i] = position;
	}
}

void Skin2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_influence_offsets", "value"), &Skin2D::set_influence_offsets);
	ClassDB::bind_method(D_METHOD("get_influence_offsets"), &Skin2D::get_influence_offsets);
	ClassDB::bind_method(D_METHOD("set_bones", "value"), &Skin2D::set_bones);
	ClassDB::bind_method(D_METHOD("get_bones"), &Skin2D::get_bones);
	ClassDB::bind_method(D_METHOD("set_bind_positions", "value"), &Skin2D::set_bind_positions);
	ClassDB::bind_method(D_METHOD("get_bind_positions"), &Skin2D::get_bind_positions);
	ClassDB::bind_method(D_METHOD("set_weights", "value"), &Skin2D::set_weights);
	ClassDB::bind_method(D_METHOD("get_weights"), &Skin2D::get_weights);
	ClassDB::bind_method(D_METHOD("set_uvs", "value"), &Skin2D::set_uvs);
	ClassDB::bind_method(D_METHOD("get_uvs"), &Skin2D::get_uvs);
	ClassDB::bind_method(D_METHOD("set_triangles", "value"), &Skin2D::set_triangles);
	ClassDB::bind_method(D_METHOD("get_triangles"), &Skin2D::get_triangles);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "influence_offsets"), "set_influence_offsets", "get_influence_offsets");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "bones"), "set_bones", "get_bones");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "bind_positions"), "set_bind_positions", "get_bind_positions");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "weights"), "set_weights", "get_weights");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "uvs"), "set_uvs", "get_uvs");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "triangles"), "set_triangles", "get_triangles");
}

void SkinnedMesh2D::changed() {
	if (skeleton) {
		skeleton->queue_pose_update();
	}
}

void SkinnedMesh2D::skin_changed() {
	geometry_dirty = true;
	changed();
}

void SkinnedMesh2D::set_skin(const Ref<Skin2D> &p_value) {
	if (skin.is_valid()) {
		skin->disconnect_changed(callable_mp(this, &SkinnedMesh2D::skin_changed));
	}
	skin = p_value;
	if (skin.is_valid()) {
		skin->connect_changed(callable_mp(this, &SkinnedMesh2D::skin_changed));
	}
	skin_changed();
}

void SkinnedMesh2D::set_texture(const Ref<Texture2D> &p_value) {
	texture = p_value;
	queue_redraw();
}

void SkinnedMesh2D::set_deformation(const PackedVector2Array &p_value) {
	deformation = p_value;
	changed();
}

bool SkinnedMesh2D::build_mesh(int p_bone_count) {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_TEX_UV] = skin->get_uvs();
	arrays[Mesh::ARRAY_INDEX] = skin->get_triangles();
	RenderingServerTypes::SurfaceData surface;
	Error error = RS::get_singleton()->mesh_create_surface_data_from_arrays(&surface, RSE::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), RSE::ARRAY_FLAG_USE_2D_VERTICES | RSE::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
	ERR_FAIL_COND_V(error != OK, false);
	vertex_bytes = surface.vertex_data;
	vertex_stride = RS::get_singleton()->mesh_surface_get_format_vertex_stride(surface.format, surface.vertex_count);
	if (!mesh.is_valid()) {
		mesh = RS::get_singleton()->mesh_create();
	} else {
		RS::get_singleton()->mesh_clear(mesh);
	}
	RS::get_singleton()->mesh_add_surface(mesh, surface);
	geometry_dirty = false;
	validated_bone_count = p_bone_count;
	queue_redraw();
	return true;
}

void SkinnedMesh2D::update_skin(const LocalVector<AffineBone2D *> &p_bones) {
	if (skin.is_null()) {
		if (mesh.is_valid()) {
			RS::get_singleton()->free(mesh);
			mesh = RID();
		}
		vertices.clear();
		queue_redraw();
		return;
	}
	if (geometry_dirty || validated_bone_count != int(p_bones.size())) {
		ERR_FAIL_COND(skin->validate(p_bones.size()) != OK);
	}
	ERR_FAIL_COND(!deformation.is_empty() && deformation.size() != skin->get_bind_positions().size());
	skin->deform(p_bones, deformation, vertices);
	if (geometry_dirty || validated_bone_count != int(p_bones.size())) {
		if (!build_mesh(p_bones.size())) {
			return;
		}
	} else {
		uint8_t *data = vertex_bytes.ptrw();
		const Vector2 *points = vertices.ptr();
		for (int i = 0; i < vertices.size(); i++) {
			encode_float(float(points[i].x), data + i * vertex_stride);
			encode_float(float(points[i].y), data + i * vertex_stride + sizeof(float));
		}
		RS::get_singleton()->mesh_surface_update_vertex_region(mesh, 0, 0, vertex_bytes);
	}
	Rect2 bounds(vertices[0], Vector2());
	for (const Vector2 &point : vertices) {
		bounds.expand_to(point);
	}
	RS::get_singleton()->mesh_set_custom_aabb(mesh, AABB(Vector3(bounds.position.x, bounds.position.y, 0), Vector3(bounds.size.x, bounds.size.y, 0)));
}

void SkinnedMesh2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		skeleton = Object::cast_to<AffineSkeleton2D>(get_parent());
		if (skeleton) {
			skeleton->structure_changed();
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		if (skeleton) {
			skeleton->structure_changed();
		}
		skeleton = nullptr;
	} else if (p_what == NOTIFICATION_DRAW && mesh.is_valid()) {
		RS::get_singleton()->canvas_item_add_mesh(get_canvas_item(), mesh, Transform2D(), Color(1, 1, 1), texture.is_valid() ? texture->get_scaled_rid() : RID());
	}
}

#ifdef DEBUG_ENABLED
Rect2 SkinnedMesh2D::_edit_get_rect() const {
	if (vertices.is_empty()) {
		return Rect2();
	}
	Rect2 bounds(vertices[0], Vector2());
	for (const Vector2 &point : vertices) {
		bounds.expand_to(point);
	}
	return bounds;
}
#endif

SkinnedMesh2D::~SkinnedMesh2D() {
	if (mesh.is_valid()) {
		RS::get_singleton()->free(mesh);
	}
	if (skin.is_valid()) {
		skin->disconnect_changed(callable_mp(this, &SkinnedMesh2D::skin_changed));
	}
}

void SkinnedMesh2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_skin", "value"), &SkinnedMesh2D::set_skin);
	ClassDB::bind_method(D_METHOD("get_skin"), &SkinnedMesh2D::get_skin);
	ClassDB::bind_method(D_METHOD("set_texture", "value"), &SkinnedMesh2D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &SkinnedMesh2D::get_texture);
	ClassDB::bind_method(D_METHOD("set_deformation", "value"), &SkinnedMesh2D::set_deformation);
	ClassDB::bind_method(D_METHOD("get_deformation"), &SkinnedMesh2D::get_deformation);
	ClassDB::bind_method(D_METHOD("set_color", "value"), &SkinnedMesh2D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &SkinnedMesh2D::get_color);
	ClassDB::bind_method(D_METHOD("get_vertices"), &SkinnedMesh2D::get_vertices);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skin", PROPERTY_HINT_RESOURCE_TYPE, "Skin2D"), "set_skin", "get_skin");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "deformation"), "set_deformation", "get_deformation");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
}
