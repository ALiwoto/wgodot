// wgodot-changes::file
#include "animation_mixer.h"

bool AnimationMixer::prepare_caches(Node *p_scope) {
	ERR_FAIL_NULL_V(p_scope, false);
	ERR_FAIL_COND_V_MSG(p_scope->is_inside_tree() || is_inside_tree(), false, "Animation preparation requires exclusive ownership of a detached hierarchy.");
	ERR_FAIL_COND_V(this != p_scope && !p_scope->is_ancestor_of(this), false);
	if (root_node.is_absolute()) {
		return false;
	}
	Node *root = get_node_or_null(root_node);
	if (!root || (root != p_scope && !p_scope->is_ancestor_of(root))) {
		return false;
	}
	LocalVector<StringName> animations;
	get_animation_list(&animations);
	for (const StringName &name : animations) {
		const Ref<Animation> &animation = get_animation(name);
		for (int track = 0; track < animation->get_track_count(); track++) {
			if (!animation->track_is_enabled(track)) {
				continue;
			}
			const NodePath path = animation->track_get_path(track);
			// Absolute paths depend on the eventual live SceneTree.
			if (path.is_absolute()) {
				return false;
			}
			Node *target = root->get_node_or_null(path);
			if (!target || (target != p_scope && !p_scope->is_ancestor_of(target))) {
				return false;
			}
		}
	}
	// Build bindings only; do not evaluate tracks or dispatch animation events.
	if (!_update_caches()) {
		return false;
	}
	caches_prepared_for_tree = true;
	return true;
}
