// wgodot-changes::file
#pragma once

#include "wgodot_export_target.h"

#include "scene/resources/packed_scene.h"

class WGodotCppProject;

// Filters serialized scene state without instantiating nodes or running scripts.
class WGodotExportScene {
	const WGodotCppProject &project;
	Vector<String> &diagnostics;
	HashMap<const PackedScene *, Ref<SceneState>> states;

public:
	WGodotExportScene(const WGodotCppProject &p_project, Vector<String> &r_diagnostics) : project(p_project), diagnostics(r_diagnostics) {}
	Ref<SceneState> filter(const Ref<PackedScene> &p_scene);
	static Ref<SceneState> filter_external(const Ref<PackedScene> &p_scene, const WGodotExportTarget &p_target, Vector<String> &r_diagnostics);
};
