// wgodot-changes::file
#include "skeleton_3d.h"

void Skeleton3D::set_serialized_bone_name(int p_bone, const String &p_name) {
	if (p_bone == get_bone_count()) {
		add_bone(p_name);
	} else {
		set_bone_name(p_bone, p_name);
	}
}
