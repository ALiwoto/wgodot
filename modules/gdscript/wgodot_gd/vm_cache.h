// wgodot-changes::file
#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

class GDScript;
class GDScriptParser;

// Compiled script snapshots are disposable, build-specific development caches.
// Runtime pointers and mutable static state never enter the disk format.
class WGodotGDScriptVMCache {
	struct Writer;
	struct Reader;

public:
	// Nested dependency loads share one metadata-validation pass.
	class LoadScope {
	public:
		LoadScope();
		~LoadScope();
	};

	static bool prepare(GDScript *p_script);
	static bool restore(GDScript *p_script, Error &r_error);
	static void store(GDScript *p_script, const GDScriptParser &p_parser);
	static void capture_dependencies(GDScript *p_script, const HashSet<String> &p_paths);
	static void clear();

private:
	static Dictionary encode(GDScript *p_script);
	static bool decode(GDScript *p_script, const Dictionary &p_snapshot, bool p_shape_only);
};
