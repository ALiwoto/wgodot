// wgodot-changes::file

#include "mesh.h"

AABB ArrayMesh::surface_get_aabb(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), AABB());
	return surfaces[p_idx].aabb;
}

Vector<AABB> ArrayMesh::surface_get_bone_aabbs(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), Vector<AABB>());
	return surfaces[p_idx].bone_aabbs;
}

Transform3D ArrayMesh::surface_get_mesh_to_skeleton_transform(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, surfaces.size(), Transform3D());
	return surfaces[p_idx].mesh_to_skeleton_xform;
}
