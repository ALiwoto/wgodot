// wgodot-changes::file
#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

// Export-time dependency and artifact cache. Never serialized into the game.
// Entries describe generator inputs/contributions, not a second copy of C++.
class WGodotCppCache {
	String directory;
	String identity;
	PackedStringArray inventory;
	Dictionary previous;
	Dictionary inputs;
	Dictionary units;
	HashMap<String, String> digests;
	HashSet<String> inspected;
	HashMap<String, bool> valid_outputs;
	bool compatible = false;
	Error scan(const String &p_directory);
	bool unchanged(const String &p_path, HashSet<String> &r_seen);

public:
	Error open(const String &p_directory, const String &p_target, bool p_trace);
	bool up_to_date();
	void track(const String &p_path);
	void set_dependencies(const String &p_path, const PackedStringArray &p_dependencies);
	String digest(const String &p_path);
	Dictionary reuse(const String &p_key, const String &p_context);
	void record(const String &p_key, const String &p_context, const PackedStringArray &p_inputs, const Dictionary &p_payload);
	bool output_matches(const String &p_name, const String &p_digest);
	Error commit(const HashMap<String, String> &p_files, int p_class_count);
	int class_count() const { return previous.get("class_count", 0); }
};
