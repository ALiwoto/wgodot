// wgodot-changes::file
#pragma once

#include "core/io/resource.h"

class KawaiiBoneCollision : public Resource {
	GDCLASS(KawaiiBoneCollision, Resource);

public:
	enum Shape {
		SHAPE_SPHERE,
		SHAPE_CAPSULE,
		SHAPE_PLANE,
	};

private:
	Shape shape = SHAPE_SPHERE;
	StringName bone_name;
	Transform3D offset;
	real_t radius = 0.0;
	real_t length = 0.0;
	bool inside = false;

protected:
	static void _bind_methods();

public:
	void set_shape(Shape p_value);
	Shape get_shape() const;
	void set_bone_name(const StringName &p_value);
	StringName get_bone_name() const;
	void set_offset(const Transform3D &p_value);
	Transform3D get_offset() const;
	void set_radius(real_t p_value);
	real_t get_radius() const;
	void set_length(real_t p_value);
	real_t get_length() const;
	void set_inside(bool p_value);
	bool get_inside() const;
};

VARIANT_ENUM_CAST(KawaiiBoneCollision::Shape);
