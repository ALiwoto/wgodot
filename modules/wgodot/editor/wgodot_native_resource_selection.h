// wgodot-changes::file
#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

class WGodotNativeResourceSelection {
	bool all_resources = true;
	HashSet<String> roots;
	String fingerprint;

public:
	Error load(const String &p_target, String &r_error);
	bool exports_all() const { return all_resources; }
	const HashSet<String> &get_roots() const { return roots; }
	const String &get_fingerprint() const { return fingerprint; }
};
