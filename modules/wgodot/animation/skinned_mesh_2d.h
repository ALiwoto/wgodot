// wgodot-changes::file
#pragma once

#include "affine_skeleton_2d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture.h"

class Skin2D : public Resource {
	GDCLASS(Skin2D, Resource);
	PackedInt32Array influence_offsets;
	PackedInt32Array bones;
	PackedVector2Array bind_positions;
	PackedFloat32Array weights;
	PackedVector2Array uvs;
	PackedInt32Array triangles;

protected:
	static void _bind_methods();

public:
	void set_influence_offsets(const PackedInt32Array &p_value);
	PackedInt32Array get_influence_offsets() const { return influence_offsets; }
	void set_bones(const PackedInt32Array &p_value);
	PackedInt32Array get_bones() const { return bones; }
	void set_bind_positions(const PackedVector2Array &p_value);
	PackedVector2Array get_bind_positions() const { return bind_positions; }
	void set_weights(const PackedFloat32Array &p_value);
	PackedFloat32Array get_weights() const { return weights; }
	void set_uvs(const PackedVector2Array &p_value);
	PackedVector2Array get_uvs() const { return uvs; }
	void set_triangles(const PackedInt32Array &p_value);
	PackedInt32Array get_triangles() const { return triangles; }
	Error validate(int p_bone_count) const;
	void deform(const LocalVector<AffineBone2D *> &p_bones, const PackedVector2Array &p_deformation, PackedVector2Array &r_vertices) const;
};

class SkinnedMesh2D : public Node2D {
	GDCLASS(SkinnedMesh2D, Node2D);
	AffineSkeleton2D *skeleton = nullptr;
	Ref<Skin2D> skin;
	Ref<Texture2D> texture;
	RID mesh;
	PackedVector2Array deformation;
	PackedVector2Array vertices;
	PackedByteArray vertex_bytes;
	uint32_t vertex_stride = 0;
	int validated_bone_count = -1;
	bool geometry_dirty = true;
	void skin_changed();
	void changed();
	bool build_mesh(int p_bone_count);

protected:
	static void _bind_methods();
	void _notification(int p_what);
#ifdef DEBUG_ENABLED
	Rect2 _edit_get_rect() const override;
	bool _edit_use_rect() const override { return true; }
#endif

public:
	void set_skin(const Ref<Skin2D> &p_value);
	Ref<Skin2D> get_skin() const { return skin; }
	void set_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_deformation(const PackedVector2Array &p_value);
	PackedVector2Array get_deformation() const { return deformation; }
	void set_color(const Color &p_value) { set_self_modulate(p_value); }
	Color get_color() const { return get_self_modulate(); }
	PackedVector2Array get_vertices() const { return vertices; }
	void update_skin(const LocalVector<AffineBone2D *> &p_bones);
	~SkinnedMesh2D();
};
