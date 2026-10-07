// wgodot-changes::file
#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

// Editor-only policy shared by native generation and packaging.
class WGodotExportTarget {
	String target;
	String fingerprint;
	struct Rule {
		String pattern;
		String owner;
	};
	Vector<Rule> owners;
	HashMap<String, Variant> settings;
	HashMap<String, String> imported_sources;
	HashSet<String> excluded_scripts;

public:
	static constexpr const char *CONFIG_PATH = "res://wgodot_targets.cfg";
	static constexpr const char *NODE_PROPERTY = "metadata/wgodot_target";

	Error load(const String &p_target, String &r_error);
	bool includes(const String &p_path) const;
	bool includes_owner(const String &p_owner) const;
	static bool valid_owner(const String &p_owner);
	String dependency_error(const String &p_path) const;
	Error read_import(const String &p_source, String &r_error);
	void add_import(const String &p_path, const String &p_source);
	void exclude_script(const String &p_path) { excluded_scripts.insert(p_path); }
	const HashSet<String> &get_excluded_scripts() const { return excluded_scripts; }
	const String &get_name() const { return target; }
	const String &get_fingerprint() const { return fingerprint; }
	const HashMap<String, Variant> &get_settings() const { return settings; }
};
