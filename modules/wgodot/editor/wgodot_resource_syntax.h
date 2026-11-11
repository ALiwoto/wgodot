// wgodot-changes::file
#pragma once

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class WGodotResourceTypes;
class WGodotExportTarget;

namespace WGodotResourceSyntax {
struct Result {
	Error error = OK;
	int line = 1;
	String message;
	Vector<int> missing_newlines;
	HashMap<String, String> dependencies;
	HashMap<String, String> declarations;
	HashMap<String, StringName> nodes;
	HashMap<String, HashSet<StringName>> properties;
};

// Parses source without loading dependencies, constructing scenes or running scripts.
Result check(const Ref<FileAccess> &p_file, const WGodotResourceTypes *p_types = nullptr, HashSet<String> *p_scene_stack = nullptr, bool p_collect_properties = false);
Error check_project(const WGodotResourceTypes &p_types, const WGodotExportTarget &p_target, Vector<String> &r_diagnostics);
} // namespace WGodotResourceSyntax
