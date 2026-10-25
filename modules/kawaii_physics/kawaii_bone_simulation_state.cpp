// wgodot-changes::file
#include "kawaii_bone_simulation_state.h"

#include "core/object/class_db.h"

void KawaiiBoneSimulationState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_chain", "root", "previous_transform", "previous_delta", "bones", "parents", "positions", "previous_positions"), &KawaiiBoneSimulationState::add_chain);
}

Error KawaiiBoneSimulationState::add_chain(const StringName &p_root, const Transform3D &p_previous_transform, double p_previous_delta,
		const PackedStringArray &p_bones, const PackedInt32Array &p_parents,
		const PackedVector3Array &p_positions, const PackedVector3Array &p_previous_positions) {
	ERR_FAIL_COND_V(p_root == StringName(), ERR_INVALID_DATA);
	ERR_FAIL_COND_V(!p_previous_transform.is_finite() || Math::is_zero_approx(p_previous_transform.basis.determinant()), ERR_INVALID_DATA);
	const int count = p_bones.size();
	ERR_FAIL_COND_V(p_parents.size() != count || p_positions.size() != count || p_previous_positions.size() != count, ERR_INVALID_DATA);
	ERR_FAIL_COND_V(!Math::is_finite(p_previous_delta) || (count > 0 && p_previous_delta <= 0), ERR_INVALID_DATA);
	for (const Chain &chain : chains) {
		ERR_FAIL_COND_V_MSG(chain.root == p_root, ERR_INVALID_DATA, "Duplicate secondary-motion chain checkpoint: " + String(p_root));
	}
	Chain chain;
	chain.root = p_root;
	chain.previous_transform = p_previous_transform;
	chain.previous_delta = p_previous_delta;
	chain.particles.resize(count);
	for (int i = 0; i < count; i++) {
		ERR_FAIL_COND_V(p_parents[i] < -1 || p_parents[i] >= i, ERR_INVALID_DATA);
		ERR_FAIL_COND_V(!p_positions[i].is_finite() || !p_previous_positions[i].is_finite(), ERR_INVALID_DATA);
		chain.particles.write[i] = { StringName(p_bones[i]), p_parents[i], p_positions[i], p_previous_positions[i] };
	}
	chains.push_back(std::move(chain));
	return OK;
}
