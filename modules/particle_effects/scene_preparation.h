// wgodot-changes::file
#pragma once

#include "scene/main/node.h"

// A preparation component is run under exclusive ownership, before the scene
// enters the live tree. It can create reusable render instances and caches.
class ScenePreparation : public Node {
	GDCLASS(ScenePreparation, Node);
	static Error _prepare_branch(Node *p_root, bool p_bindings);
	static int _prepare_animation_caches(Node *p_node, Node *p_scope);

protected:
	static void _bind_methods();
	GDVIRTUAL0R(Error, _prepare_resources);
	// Runs after every resource preparer in the branch, including components
	// which create child nodes. Cache cross-component bindings in this phase.
	GDVIRTUAL0R(Error, _prepare_bindings);

public:
	static Error prepare_branch(Node *p_root);
	// Run after customization has finalized the branch's nodes/materials.
	static int prepare_animation_caches(Node *p_root);
	// Takes exclusive ownership. Nodes must already be detached from the tree
	// and no longer referenced by active gameplay or callbacks.
	static void free_branch_async(Node *p_root);
	static void free_branches_async(const Vector<Node *> &p_roots);
	static void finish_disposals();
};
