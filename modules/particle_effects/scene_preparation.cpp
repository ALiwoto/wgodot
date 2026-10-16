// wgodot-changes::file
#include "core/object/class_db.h"
#include "scene/animation/animation_mixer.h"
#include "scene_preparation.h"

void ScenePreparation::_bind_methods() {
	ClassDB::bind_static_method("ScenePreparation", D_METHOD("prepare_branch", "root"), &ScenePreparation::prepare_branch);
	ClassDB::bind_static_method("ScenePreparation", D_METHOD("prepare_animation_caches", "root"), &ScenePreparation::prepare_animation_caches);
	ClassDB::bind_static_method("ScenePreparation", D_METHOD("free_branch_async", "root"), &ScenePreparation::free_branch_async);
	GDVIRTUAL_BIND(_prepare_resources);
	GDVIRTUAL_BIND(_prepare_bindings);
}

Error ScenePreparation::prepare_branch(Node *p_root) {
	ERR_FAIL_NULL_V(p_root, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(p_root->is_inside_tree(), ERR_ALREADY_IN_USE, "Scene preparation requires exclusive ownership of an off-tree hierarchy.");
	Error error = _prepare_branch(p_root, false);
	if (error != OK) {
		return error;
	}
	return _prepare_branch(p_root, true);
}

Error ScenePreparation::_prepare_branch(Node *p_root, bool p_bindings) {
	for (int i = 0; i < p_root->get_child_count(); ++i) {
		const Error error = _prepare_branch(p_root->get_child(i), p_bindings);
		if (error != OK) {
			return error;
		}
	}
	ScenePreparation *preparation = Object::cast_to<ScenePreparation>(p_root);
	Error error = OK;
	if (preparation) {
		if (p_bindings) {
			preparation->GDVIRTUAL_CALL(_prepare_bindings, error);
		} else {
			preparation->GDVIRTUAL_CALL(_prepare_resources, error);
		}
	}
	return error;
}

int ScenePreparation::prepare_animation_caches(Node *p_root) {
	ERR_FAIL_NULL_V(p_root, 0);
	ERR_FAIL_COND_V_MSG(p_root->is_inside_tree(), 0, "Animation preparation requires exclusive ownership of a detached hierarchy.");
	return _prepare_animation_caches(p_root, p_root);
}

int ScenePreparation::_prepare_animation_caches(Node *p_node, Node *p_scope) {
	int prepared = 0;
	AnimationMixer *mixer = Object::cast_to<AnimationMixer>(p_node);
	if (mixer && mixer->prepare_caches(p_scope)) {
		prepared++;
	}
	for (int child = 0; child < p_node->get_child_count(); child++) {
		prepared += _prepare_animation_caches(p_node->get_child(child), p_scope);
	}
	return prepared;
}
