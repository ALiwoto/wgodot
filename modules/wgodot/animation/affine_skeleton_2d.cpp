// wgodot-changes::file
#include "affine_skeleton_2d.h"
#include "skinned_mesh_2d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void AffineBone2D::changed() {
	if (skeleton) {
		skeleton->queue_pose_update();
	}
}

void AffineBone2D::_notification(int p_what) {
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
	}
}

bool AffineBone2D::evaluate(const LocalVector<AffineBone2D *> &p_bones) {
	if (visit == 2) {
		return true;
	}
	ERR_FAIL_COND_V_MSG(visit == 1, false, "Cyclic 2D bone/constraint dependency.");
	visit = 1;
	real_t x_angle = pose_rotation + pose_shear.x;
	real_t y_angle = pose_rotation + pose_shear.y + Math::PI / 2;
	Transform2D local(Vector2(Math::cos(x_angle), Math::sin(x_angle)) * pose_scale.x,
			Vector2(Math::cos(y_angle), Math::sin(y_angle)) * pose_scale.y, pose_position);
	pose = local;
	if (parent_bone >= 0) {
		ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(parent_bone), p_bones.size(), false);
		AffineBone2D *parent = p_bones[parent_bone];
		if (!parent->evaluate(p_bones)) {
			return false;
		}
		const Transform2D &p = parent->pose;
		pose = p * local;
		if (inheritance == INHERIT_TRANSLATION) {
			pose[0] = local[0];
			pose[1] = local[1];
		} else if (inheritance == INHERIT_NO_ROTATION_OR_REFLECTION) {
			real_t a = p[0].x;
			real_t c = p[0].y;
			real_t b = p[1].x;
			real_t d = p[1].y;
			real_t squared_length = a * a + c * c;
			real_t parent_rotation;
			if (squared_length > 0.0001) {
				real_t perpendicular_scale = Math::abs(p.determinant()) / squared_length;
				b = c * perpendicular_scale;
				d = a * perpendicular_scale;
				parent_rotation = Math::atan2(c, a);
			} else {
				a = 0;
				c = 0;
				parent_rotation = Math::PI / 2 - Math::atan2(d, b);
			}
			Transform2D inherited(Vector2(a, c), Vector2(-b, d), Vector2());
			pose[0] = inherited.basis_xform(Vector2(Math::cos(x_angle - parent_rotation), Math::sin(x_angle - parent_rotation)) * pose_scale.x);
			pose[1] = inherited.basis_xform(Vector2(Math::cos(y_angle - parent_rotation), Math::sin(y_angle - parent_rotation)) * pose_scale.y);
		} else if (inheritance == INHERIT_NO_SCALE || inheritance == INHERIT_NO_SCALE_OR_REFLECTION) {
			Vector2 axis = p.basis_xform(Vector2(Math::cos(pose_rotation), Math::sin(pose_rotation)));
			real_t length = axis.length();
			axis *= length > 0.00001 ? 1 / length : length;
			Vector2 perpendicular(-axis.y, axis.x);
			if (inheritance == INHERIT_NO_SCALE && p.determinant() < 0) {
				perpendicular = -perpendicular;
			}
			Transform2D inherited(axis, perpendicular, Vector2());
			pose[0] = inherited.basis_xform(Vector2(Math::cos(pose_shear.x), Math::sin(pose_shear.x)) * pose_scale.x);
			pose[1] = inherited.basis_xform(Vector2(-Math::sin(pose_shear.y), Math::cos(pose_shear.y)) * pose_scale.y);
		}
	}
	if (translation_target >= 0 && translation_mix != 0) {
		ERR_FAIL_UNSIGNED_INDEX_V(uint32_t(translation_target), p_bones.size(), false);
		AffineBone2D *target = p_bones[translation_target];
		if (!target->evaluate(p_bones)) {
			return false;
		}
		pose[2] = pose[2].lerp(target->pose.xform(translation_offset), translation_mix);
	}
	visit = 2;
	return true;
}

#define POSE_SETTER(m_type, m_name) \
	void AffineBone2D::set_##m_name(m_type p_value) { \
		if (m_name == p_value) { \
			return; \
		} \
		m_name = p_value; \
		changed(); \
	}
POSE_SETTER(int, parent_bone)
POSE_SETTER(AffineBone2D::Inheritance, inheritance)
POSE_SETTER(const Vector2 &, pose_position)
POSE_SETTER(real_t, pose_rotation)
POSE_SETTER(const Vector2 &, pose_scale)
POSE_SETTER(const Vector2 &, pose_shear)
POSE_SETTER(int, translation_target)
POSE_SETTER(const Vector2 &, translation_offset)
POSE_SETTER(real_t, translation_mix)
#undef POSE_SETTER

void AffineBone2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parent_bone", "value"), &AffineBone2D::set_parent_bone);
	ClassDB::bind_method(D_METHOD("get_parent_bone"), &AffineBone2D::get_parent_bone);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "parent_bone"), "set_parent_bone", "get_parent_bone");
	ClassDB::bind_method(D_METHOD("set_pose_position", "value"), &AffineBone2D::set_pose_position);
	ClassDB::bind_method(D_METHOD("get_pose_position"), &AffineBone2D::get_pose_position);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pose_position"), "set_pose_position", "get_pose_position");
	ClassDB::bind_method(D_METHOD("set_pose_rotation", "value"), &AffineBone2D::set_pose_rotation);
	ClassDB::bind_method(D_METHOD("get_pose_rotation"), &AffineBone2D::get_pose_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pose_rotation"), "set_pose_rotation", "get_pose_rotation");
	ClassDB::bind_method(D_METHOD("set_pose_scale", "value"), &AffineBone2D::set_pose_scale);
	ClassDB::bind_method(D_METHOD("get_pose_scale"), &AffineBone2D::get_pose_scale);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pose_scale"), "set_pose_scale", "get_pose_scale");
	ClassDB::bind_method(D_METHOD("set_pose_shear", "value"), &AffineBone2D::set_pose_shear);
	ClassDB::bind_method(D_METHOD("get_pose_shear"), &AffineBone2D::get_pose_shear);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pose_shear"), "set_pose_shear", "get_pose_shear");
	ClassDB::bind_method(D_METHOD("set_translation_target", "value"), &AffineBone2D::set_translation_target);
	ClassDB::bind_method(D_METHOD("get_translation_target"), &AffineBone2D::get_translation_target);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "translation_target"), "set_translation_target", "get_translation_target");
	ClassDB::bind_method(D_METHOD("set_translation_offset", "value"), &AffineBone2D::set_translation_offset);
	ClassDB::bind_method(D_METHOD("get_translation_offset"), &AffineBone2D::get_translation_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "translation_offset"), "set_translation_offset", "get_translation_offset");
	ClassDB::bind_method(D_METHOD("set_translation_mix", "value"), &AffineBone2D::set_translation_mix);
	ClassDB::bind_method(D_METHOD("get_translation_mix"), &AffineBone2D::get_translation_mix);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "translation_mix"), "set_translation_mix", "get_translation_mix");
	ClassDB::bind_method(D_METHOD("set_inheritance", "value"), &AffineBone2D::set_inheritance);
	ClassDB::bind_method(D_METHOD("get_inheritance"), &AffineBone2D::get_inheritance);
	ClassDB::bind_method(D_METHOD("get_pose"), &AffineBone2D::get_pose);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "inheritance", PROPERTY_HINT_ENUM, "All,Translation,No Rotation or Reflection,No Scale,No Scale or Reflection"), "set_inheritance", "get_inheritance");
	BIND_ENUM_CONSTANT(INHERIT_ALL);
	BIND_ENUM_CONSTANT(INHERIT_TRANSLATION);
	BIND_ENUM_CONSTANT(INHERIT_NO_ROTATION_OR_REFLECTION);
	BIND_ENUM_CONSTANT(INHERIT_NO_SCALE);
	BIND_ENUM_CONSTANT(INHERIT_NO_SCALE_OR_REFLECTION);
}

void AffineSkeleton2D::structure_changed() {
	structure_dirty = true;
	queue_pose_update();
}

void AffineSkeleton2D::rebuild() {
	bones.clear();
	meshes.clear();
	for (int i = 0; i < get_child_count(); i++) {
		if (auto *bone = Object::cast_to<AffineBone2D>(get_child(i))) {
			bones.push_back(bone);
		} else if (auto *mesh = Object::cast_to<SkinnedMesh2D>(get_child(i))) {
			meshes.push_back(mesh);
		}
	}
	structure_dirty = false;
}

void AffineSkeleton2D::queue_pose_update() {
	if (!queued && is_inside_tree()) {
		queued = true;
		callable_mp(this, &AffineSkeleton2D::update_pose).call_deferred();
	}
}

void AffineSkeleton2D::update_pose() {
	queued = false;
	if (!is_inside_tree()) {
		return;
	}
	if (structure_dirty) {
		rebuild();
	}
	for (AffineBone2D *bone : bones) {
		bone->visit = 0;
	}
	for (AffineBone2D *bone : bones) {
		if (!bone->evaluate(bones)) {
			return;
		}
	}
	for (SkinnedMesh2D *mesh : meshes) {
		mesh->update_skin(bones);
	}
}

void AffineSkeleton2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		structure_changed();
		update_pose();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		bones.clear();
		meshes.clear();
		structure_dirty = true;
	}
}

void AffineSkeleton2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("update_pose"), &AffineSkeleton2D::update_pose);
}
