// wgodot-changes::file
#pragma once

#include "core/object/property_info.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

class WGodotResourceExport {
	struct Entry {
		int64_t target = 0;
		uint32_t format = 0;
		String type;
		HashMap<String, int64_t> variants;
	};
	HashMap<String, int64_t> ids;
	HashMap<int64_t, Entry> entries;
	HashSet<int64_t> payloads;
	HashSet<int64_t> native_resources;
	HashSet<int64_t> required;
	HashMap<uint32_t, String> formats;
	int64_t next_id = 1;
	Error error = OK;

	int64_t identify(const String &p_path);
	String rewrite_text(const String &p_text, const String &p_source);
	Error rewrite_binary(const String &p_source, Vector<uint8_t> &r_data);
	Error import_remap(const String &p_source, const Vector<uint8_t> &p_data);

public:
	void initialize(const Dictionary &p_paths);
	void add_native_resource(const String &p_path, const String &p_type, const String &p_target = String());
	String path(const String &p_original);
	Variant rewrite_value(const Variant &p_value, const PropertyInfo &p_property = PropertyInfo());
	Error export_file(String &r_path, Vector<uint8_t> &r_data);
	Error finish(Vector<uint8_t> &r_catalog);
};
