// wgodot-changes::file
#pragma once

#include "core/os/thread.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace WGodotSceneTree {

template <typename Matches>
void collect(Node *p_node, const Matches &p_matches, Vector<ObjectID> &r_nodes) {
	if (p_matches(p_node)) {
		r_nodes.push_back(p_node->get_instance_id());
	}
	// No user methods run while traversing. Include internal nodes and descend
	// through nonmatching nodes, in parent-first scene order.
	for (Node *child : p_node->iterate_children<true>()) {
		collect(child, p_matches, r_nodes);
	}
}

template <typename Matches, typename Invoke>
void call_group_as(SceneTree *p_tree, const Matches &p_matches, const Invoke &p_invoke) {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "SceneTree.call_group_as() must run on the main thread.");
	Vector<ObjectID> nodes;
	collect(p_tree->get_root(), p_matches, nodes);
	for (ObjectID id : nodes) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
		// Earlier calls may free, remove, or change the script of a later node.
		if (node && node->is_inside_tree() && node->get_tree() == p_tree && p_matches(node)) {
			p_invoke(node);
		}
	}
}

} // namespace WGodotSceneTree
