// wgodot-changes::file
#pragma once

#include "scene/resources/packed_scene.h"

// Export-only index into one SceneState's instantiation table. Nested scenes keep
// their own indices; never substitute an index from a different scene's table.
class WGodotCppSceneReferences {
	HashMap<NodePath, int> indices;
	Vector<Vector<StringName>> paths;

	static NodePath key(const Vector<StringName> &p_names);

public:
	static constexpr int RUNTIME_PATH = -2;
	static constexpr int NULL_NODE = -1;
	void prepare(const Ref<SceneState> &p_state);
	int find(int p_from, const NodePath &p_path) const;
};
