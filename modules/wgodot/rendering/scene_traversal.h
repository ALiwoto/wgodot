// wgodot-changes::file
#pragma once
#ifdef TOOLS_ENABLED
#include "capture_paths.h"
#include "scene_snapshot.h"
#include "core/templates/local_vector.h"

class Viewport;

// Main-thread traversal of a held scene. Built-in state is copied into typed
// snapshot records; next() yields only nodes needing project-specific fields.
// The snapshot itself never owns live nodes and can serialize on a worker.
class WSceneTraversal : public RefCounted {
	GDCLASS(WSceneTraversal, RefCounted);
	LocalVector<Node *> pending;
	Node *root = nullptr;
	Viewport *viewport = nullptr;
	Ref<WSceneSnapshot> output;
	Ref<WCapturePaths> paths;
	Ref<ConfigFile> palettes;
	bool world = false;
	bool recursive = true;
	void append_children(Node *p_node);
	bool capture(Node *p_node);

protected:
	static void _bind_methods();

public:
	void begin(Node *p_root, const TypedArray<Node> &p_roots, const Ref<WSceneSnapshot> &p_output,
			const Ref<WCapturePaths> &p_paths, const Ref<ConfigFile> &p_palettes, bool p_world, bool p_recursive = true);
	Node *next(uint64_t p_deadline_usec = 0);
	bool is_completed() const { return pending.is_empty(); }
};
#endif
