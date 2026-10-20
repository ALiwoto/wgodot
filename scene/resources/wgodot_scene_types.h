// wgodot-changes::file
#pragma once

#include "scene/resources/packed_scene.h"
#include "core/object/script_language.h"

namespace WGodotSceneTypes {
struct Type {
	StringName native;
	Ref<Script> script;
};
// Instances authored earlier in the text, indexed without walking ordinary nodes.
class InheritedTypes {
	HashMap<String, Ref<SceneState>> instances;

public:
	void add(const String &p_path, const Ref<SceneState> &p_state);
	Type resolve(const String &p_path) const;
};
Type resolve(const Ref<SceneState> &p_state, const NodePath &p_path);
String validate(const Ref<SceneState> &p_state);
} // namespace WGodotSceneTypes
