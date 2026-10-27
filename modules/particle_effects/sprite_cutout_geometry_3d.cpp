// wgodot-changes::file
#include "sprite_cutout_geometry_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#include <cstring>

void SpriteCutoutGeometry3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_vertices_per_frame", "count"), &SpriteCutoutGeometry3D::set_vertices_per_frame);
	ClassDB::bind_method(D_METHOD("get_vertices_per_frame"), &SpriteCutoutGeometry3D::get_vertices_per_frame);
	ClassDB::bind_method(D_METHOD("set_points", "points"), &SpriteCutoutGeometry3D::set_points);
	ClassDB::bind_method(D_METHOD("get_points"), &SpriteCutoutGeometry3D::get_points);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "vertices_per_frame", PROPERTY_HINT_RANGE, "3,65535,1"), "set_vertices_per_frame", "get_vertices_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "points"), "set_points", "get_points");
}

void SpriteCutoutGeometry3D::set_vertices_per_frame(int p_count) {
	ERR_FAIL_COND(p_count < 3 || p_count > 65535);
	if (vertices_per_frame == p_count) {
		return;
	}
	vertices_per_frame = p_count;
	emit_changed();
}

void SpriteCutoutGeometry3D::set_points(const Vector<Vector2> &p_points) {
	for (const Vector2 &point : p_points) {
		ERR_FAIL_COND_MSG(!point.is_finite(), "Cutout vertices must be finite.");
	}
	points = p_points;
	emit_changed();
}

void SpriteCutoutMesh3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_geometry", "geometry"), &SpriteCutoutMesh3D::set_geometry);
	ClassDB::bind_method(D_METHOD("get_geometry"), &SpriteCutoutMesh3D::get_geometry);
	ClassDB::bind_method(D_METHOD("set_frame", "frame"), &SpriteCutoutMesh3D::set_frame);
	ClassDB::bind_method(D_METHOD("get_frame"), &SpriteCutoutMesh3D::get_frame);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "geometry", PROPERTY_HINT_RESOURCE_TYPE, "SpriteCutoutGeometry3D"), "set_geometry", "get_geometry");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame", PROPERTY_HINT_RANGE, "-1,65535,1,or_greater"), "set_frame", "get_frame");
}

void SpriteCutoutMesh3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "_surfaces") {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

void SpriteCutoutMesh3D::set_geometry(const Ref<SpriteCutoutGeometry3D> &p_geometry) {
	if (geometry == p_geometry) {
		return;
	}
	if (geometry.is_valid()) {
		geometry->disconnect_changed(callable_mp(this, &SpriteCutoutMesh3D::_rebuild));
	}
	geometry = p_geometry;
	if (geometry.is_valid()) {
		geometry->connect_changed(callable_mp(this, &SpriteCutoutMesh3D::_rebuild));
	}
	_rebuild();
}

void SpriteCutoutMesh3D::set_frame(int p_frame) {
	ERR_FAIL_COND(p_frame < -1);
	if (frame == p_frame) {
		return;
	}
	frame = p_frame;
	_rebuild();
}

void SpriteCutoutMesh3D::_rebuild() {
	if (get_surface_count()) {
		clear_surfaces();
	}
	// Resource fields may arrive in either order. No work until both are set.
	if (geometry.is_null() || frame < 0 || geometry->get_point_data().is_empty()) {
		return;
	}
	const int count = geometry->get_vertices_per_frame();
	const Vector<Vector2> &points = geometry->get_point_data();
	ERR_FAIL_COND_MSG(points.size() % count != 0, "Cutout geometry contains an incomplete polygon.");
	ERR_FAIL_INDEX(frame, points.size() / count);
	const Vector2 *polygon = points.ptr() + frame * count;
	const int index_count = (count - 2) * 3;
	const BitField<ArrayFormat> format = ARRAY_FORMAT_VERTEX | ARRAY_FORMAT_TEX_UV | ARRAY_FORMAT_TEX_UV2 | ARRAY_FORMAT_INDEX | ARRAY_FLAG_FORMAT_CURRENT_VERSION;
	uint32_t offsets[RSE::ARRAY_MAX];
	uint32_t vertex_stride, normal_stride, attribute_stride, skin_stride;
	RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(format, count, index_count, offsets, vertex_stride, normal_stride, attribute_stride, skin_stride);
	Vector<uint8_t> vertices;
	Vector<uint8_t> attributes;
	Vector<uint8_t> indices;
	vertices.resize(count * vertex_stride);
	attributes.resize(count * attribute_stride);
	indices.resize(index_count * sizeof(uint16_t));
	uint8_t *vertex_data = vertices.ptrw();
	uint8_t *attribute_data = attributes.ptrw();
	uint8_t *index_data = indices.ptrw();
	AABB bounds;
	for (int i = 0; i < count; i++) {
		const Vector2 uv = polygon[i];
		const Vector2 reversed = polygon[count - i - 1];
		const float position[3] = { float(uv.x - 0.5), float(0.5 - uv.y), 0.0f };
		const float texcoord[2] = { float(uv.x), float(uv.y) };
		const float mirror[2] = { float(reversed.x), float(reversed.y) };
		memcpy(vertex_data + offsets[RSE::ARRAY_VERTEX] + i * vertex_stride, position, sizeof(position));
		memcpy(attribute_data + offsets[RSE::ARRAY_TEX_UV] + i * attribute_stride, texcoord, sizeof(texcoord));
		memcpy(attribute_data + offsets[RSE::ARRAY_TEX_UV2] + i * attribute_stride, mirror, sizeof(mirror));
		const Vector3 point(position[0], position[1], position[2]);
		if (i == 0) {
			bounds.position = point;
		} else {
			bounds.expand_to(point);
		}
	}
	for (int i = 0; i < count - 2; i++) {
		// Godot clockwise winding, matching the previous glTF import.
		const uint16_t triangle[3] = { uint16_t(i + 2), uint16_t(i + 1), 0 };
		memcpy(index_data + i * sizeof(triangle), triangle, sizeof(triangle));
	}
	add_surface(format, PRIMITIVE_TRIANGLES, vertices, attributes, Vector<uint8_t>(), count, indices, index_count, bounds);
}

SpriteCutoutMesh3D::~SpriteCutoutMesh3D() {
	if (geometry.is_valid()) {
		geometry->disconnect_changed(callable_mp(this, &SpriteCutoutMesh3D::_rebuild));
	}
}
