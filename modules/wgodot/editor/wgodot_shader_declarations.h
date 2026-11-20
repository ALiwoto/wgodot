// wgodot-changes::file
#pragma once

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"

#include <memory>

// One cache per resource check. Different shaders parse concurrently; consumers
// of the same shader share its declarations, never a material or GPU object.
class WGodotShaderDeclarations {
	struct Entry {
		Mutex mutex;
		bool ready = false;
		Error error = OK;
		String message;
		HashMap<StringName, PropertyInfo> uniforms;
		HashMap<StringName, PropertyInfo> instance_uniforms;
		HashMap<String, String> dependencies;
	};
	mutable Mutex mutex;
	mutable HashMap<String, std::shared_ptr<Entry>> entries;
	static void parse(const String &p_path, Entry &r_entry);

public:
	Error uniform(const String &p_path, const StringName &p_name, PropertyInfo &r_info, HashMap<String, String> &r_dependencies, String &r_error, bool p_instance = false) const;
};
