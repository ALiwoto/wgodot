// wgodot-changes::file

#include "mesh_instance_3d.h"
#include "skeleton_3d.h"

uint32_t SkinReference::get_resolved_bind_count() const {
	return skeleton_node && skeleton_version == skeleton_node->get_version() ? skin_bone_indices.size() : 0;
}

Transform3D SkinReference::get_bind_transform(uint32_t p_bind) const {
	ERR_FAIL_NULL_V(skeleton_node, Transform3D());
	ERR_FAIL_UNSIGNED_INDEX_V(p_bind, skin_bone_indices.size(), Transform3D());
	const int bone = skin_bone_indices[p_bind];
	// Match Skeleton3D's palette, including per-bone skin scaling. Read the
	// scene pose directly: no RenderingServer synchronization or GPU readback.
	const Transform3D bind = skin->get_bind_pose(p_bind).scaled(skeleton_node->get_bone_skin_scale(bone));
	return skeleton_node->get_bone_global_pose(bone) * bind;
}

AABB MeshInstance3D::get_pose_aabb() const {
	const ArrayMesh *array_mesh = Object::cast_to<ArrayMesh>(mesh.ptr());
	if (!array_mesh) {
		return get_aabb();
	}
	if (skin_ref.is_null() || skin_ref->get_resolved_bind_count() == 0) {
		return get_aabb();
	}
	AABB result;
	for (int surface = 0; surface < array_mesh->get_surface_count(); surface++) {
		AABB bounds;
		bool found = false;
		const Vector<AABB> bones = array_mesh->surface_get_bone_aabbs(surface);
		const Transform3D to_skeleton = array_mesh->surface_get_mesh_to_skeleton_transform(surface);
		if ((array_mesh->surface_get_format(surface) & Mesh::ARRAY_FORMAT_BONES) && uint32_t(bones.size()) <= skin_ref->get_resolved_bind_count()) {
			for (int bind = 0; bind < bones.size(); bind++) {
				if (bones[bind].size == Vector3(-1, -1, -1)) {
					continue;
				}
				const AABB posed = skin_ref->get_bind_transform(bind).xform(to_skeleton.xform(bones[bind]));
				if (found) {
					bounds.merge_with(posed);
				} else {
					bounds = posed;
					found = true;
				}
			}
		}
		if (found) {
			bounds = to_skeleton.affine_inverse().xform(bounds);
		}
		if (!found || bounds.size == Vector3()) {
			bounds = array_mesh->surface_get_aabb(surface);
		}
		if (surface == 0) {
			result = bounds;
		} else {
			result.merge_with(bounds);
		}
	}
	return result;
}
