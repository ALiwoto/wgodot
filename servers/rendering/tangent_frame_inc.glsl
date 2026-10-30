// wgodot-changes::file
#ifdef TANGENT_FRAME_SCALE_INDEPENDENT
// Remove object scale from the transform, not from the deformed vertex frame.
// This retains authored/skinned vector lengths and mirrored-axis signs.
mat3 scale_independent_tangent_basis(mat3 basis) {
	vec3 inverse_scale = inversesqrt(max(vec3(
			dot(basis[0], basis[0]), dot(basis[1], basis[1]), dot(basis[2], basis[2])), vec3(1e-30)));
	return mat3(basis[0] * inverse_scale.x, basis[1] * inverse_scale.y, basis[2] * inverse_scale.z);
}
#endif
