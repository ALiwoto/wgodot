// wgodot-changes::file
#pragma once
#include "core/io/file_access.h"
#include "core/templates/hash_map.h"

class WGodotResourcePack {
	struct Entry {
		String path;
		uint64_t offset = 0;
		uint64_t size = 0;
		Vector<uint8_t> md5;
	};
	Ref<FileAccess> source;
	Vector<Entry> entries;

public:
	Error open(const String &p_path);
	PackedByteArray read(const String &p_path) const;
	Error rewrite(const String &p_destination, const HashMap<String, String> &p_replacements, const String &p_identity_path, const PackedByteArray &p_identity);
	void close() { source.unref(); }
};
