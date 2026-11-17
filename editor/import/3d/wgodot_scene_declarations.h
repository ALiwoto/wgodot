// wgodot-changes::file
#pragma once

#include "core/templates/hash_map.h"
#include "scene/resources/packed_scene.h"

namespace WGodotSceneDeclarations {
String path_for_source(const String &p_source);
Error save(const Ref<SceneState> &p_state, const String &p_path);
Error read(const String &p_path, HashMap<String, StringName> &r_nodes);
}
