// wgodot-changes::file
#pragma once

#include "scene/2d/node_2d.h"
#include "core/templates/local_vector.h"

class SkinnedMesh2D;
class AffineSkeleton2D;

// A pose bone, independent of CanvasItem transforms. Skin influences carry
// their own bind positions, so neither four-weight truncation nor a single
// inverse-bind matrix is imposed on authored 2D meshes.
class AffineBone2D : public Node {
	GDCLASS(AffineBone2D, Node);

public:
	enum Inheritance {
		INHERIT_ALL,
		INHERIT_TRANSLATION,
		INHERIT_NO_ROTATION_OR_REFLECTION,
		INHERIT_NO_SCALE,
		INHERIT_NO_SCALE_OR_REFLECTION,
	};

private:
	friend class AffineSkeleton2D;
	AffineSkeleton2D *skeleton = nullptr;
	int parent_bone = -1;
	Inheritance inheritance = INHERIT_ALL;
	Vector2 pose_position;
	real_t pose_rotation = 0;
	Vector2 pose_scale = Vector2(1, 1);
	Vector2 pose_shear;
	int translation_target = -1;
	Vector2 translation_offset;
	real_t translation_mix = 0;
	Transform2D pose;
	uint8_t visit = 0;
	bool evaluate(const LocalVector<AffineBone2D *> &p_bones);
	void changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_parent_bone(int p_value);
	int get_parent_bone() const { return parent_bone; }
	void set_inheritance(Inheritance p_value);
	Inheritance get_inheritance() const { return inheritance; }
	void set_pose_position(const Vector2 &p_value);
	Vector2 get_pose_position() const { return pose_position; }
	void set_pose_rotation(real_t p_value);
	real_t get_pose_rotation() const { return pose_rotation; }
	void set_pose_scale(const Vector2 &p_value);
	Vector2 get_pose_scale() const { return pose_scale; }
	void set_pose_shear(const Vector2 &p_value);
	Vector2 get_pose_shear() const { return pose_shear; }
	void set_translation_target(int p_value);
	int get_translation_target() const { return translation_target; }
	void set_translation_offset(const Vector2 &p_value);
	Vector2 get_translation_offset() const { return translation_offset; }
	void set_translation_mix(real_t p_value);
	real_t get_translation_mix() const { return translation_mix; }
	Transform2D get_pose() const { return pose; }
	const Transform2D &pose_transform() const { return pose; }
};

VARIANT_ENUM_CAST(AffineBone2D::Inheritance);

class AffineSkeleton2D : public Node2D {
	GDCLASS(AffineSkeleton2D, Node2D);
	LocalVector<AffineBone2D *> bones;
	LocalVector<SkinnedMesh2D *> meshes;
	bool queued = false;
	bool structure_dirty = true;
	void rebuild();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void queue_pose_update();
	void structure_changed();
	void update_pose();
};
