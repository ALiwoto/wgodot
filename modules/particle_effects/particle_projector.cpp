// wgodot-changes::file
#include "particle_instance_3d.h"

void ParticleInstance3D::set_projector_scale_axes(const Vector3i &p_axes) {
	ERR_FAIL_COND_MSG(p_axes != Vector3i(-1, -1, -1) &&
			(p_axes.x < 0 || p_axes.x > 2 || p_axes.y < 0 || p_axes.y > 2 || p_axes.z < 0 || p_axes.z > 2 ||
					p_axes.x == p_axes.y || p_axes.x == p_axes.z || p_axes.y == p_axes.z),
			"Projector scale axes must be a permutation of (0, 1, 2), or (-1, -1, -1) for normal transform inheritance.");
	reset_projector();
	projector_scale_axes = p_axes;
}

void ParticleInstance3D::prepare_projector() {
	if (projector_scale_axes.x < 0) {
		return;
	}
	Node3D *projector = decal ? static_cast<Node3D *>(decal) : geometry;
	ERR_FAIL_NULL_MSG(projector, "Independent projector scaling requires a decal or geometry binding.");
	projector_local_transform = projector->get_transform();
	projector->set_as_top_level(true);
	projector_prepared = true;
}

void ParticleInstance3D::update_projector() {
	if (!projector_prepared) {
		return;
	}
	Node3D *projector = decal ? static_cast<Node3D *>(decal) : geometry;
	Node3D *parent = Object::cast_to<Node3D>(projector->get_parent());
	const Transform3D parent_transform = get_render_transform(parent);
	if (parent_transform.basis.determinant() == 0.0 || projector_local_transform.basis.determinant() == 0.0) {
		// A collapsed projector has no volume or recoverable orientation.
		// Keep it collapsed without asking quaternion decomposition to invert it.
		projector->set_transform(Transform3D(Basis().scaled(Vector3()), parent_transform.xform(projector_local_transform.origin)));
		return;
	}
	const Vector3 parent_scale = parent_transform.basis.get_scale();
	const Vector3 scale = Vector3(parent_scale[projector_scale_axes.x], parent_scale[projector_scale_axes.y], parent_scale[projector_scale_axes.z]) * projector_local_transform.basis.get_scale();
	// Projector dimensions follow their own axes after rotation, without the
	// shear produced by ordinary parent-scale * child-rotation composition.
	const Basis rotation = Basis(parent_transform.basis.get_rotation_quaternion() * projector_local_transform.basis.get_rotation_quaternion());
	projector->set_transform(Transform3D(rotation.scaled_local(scale), parent_transform.xform(projector_local_transform.origin)));
}

void ParticleInstance3D::reset_projector() {
	if (!projector_prepared) {
		return;
	}
	Node3D *projector = decal ? static_cast<Node3D *>(decal) : geometry;
	projector->set_as_top_level(false);
	projector->set_transform(projector_local_transform);
	projector_prepared = false;
}
