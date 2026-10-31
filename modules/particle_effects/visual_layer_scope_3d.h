// wgodot-changes::file
#pragma once

#include "core/templates/hash_set.h"
#include "scene/3d/visual_instance_3d.h"

// Temporarily adds camera layer bits to a subject and its prepared instances,
// retaining visible scene lighting. The caller reserves those bits exclusively.
class VisualLayerScope3D : public Node {
	GDCLASS(VisualLayerScope3D, Node);

	struct VisualState {
		VisualInstance3D *visual = nullptr;
		uint32_t previous_bits = 0;
	};
	// Private visuals belong to their live pool. Restore/remove them when that
	// pool exits, before its detached instances can be destroyed on a worker.
	HashMap<Node *, Vector<VisualState>> owners;
	Node3D *subject = nullptr;
	HashSet<Node3D *> additional_subjects;
	SceneTree *active_tree = nullptr;
	uint32_t layers = 0;
	uint32_t light_mask = 0;

	void include_visual(VisualInstance3D *p_visual, Node *p_owner, bool p_subject);
	void include_private(Node *p_node, Node *p_owner, bool p_subject);
	void include_node(Node *p_node, bool p_subject);
	void include_branch(Node *p_node, bool p_subject);
	void node_added(Node *p_node);
	void node_removed(Node *p_node);
	void restore(Node *p_owner);
	bool contains_subject(Node *p_node) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void begin(Node3D *p_subject, uint32_t p_layers, uint32_t p_light_mask);
	void add_subject(Node3D *p_subject);
	void remove_subject(Node3D *p_subject);
	void end();
};
