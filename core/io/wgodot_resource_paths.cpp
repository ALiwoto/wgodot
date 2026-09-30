// wgodot-changes::file
#include "wgodot_resource_paths.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

namespace WGodotResourcePaths {
namespace {
struct Entry {
	int64_t target = 0;
	uint32_t format = 0;
	StringName type;
	Vector<Pair<String, int64_t>> variants;
};

struct Catalog {
	HashMap<int64_t, Entry> entries;

	Catalog() {
		Ref<FileAccess> file = FileAccess::open(CATALOG_PATH, FileAccess::READ);
		ERR_FAIL_COND_MSG(file.is_null(), "Native resource catalog is missing.");
		ERR_FAIL_COND_MSG(file->get_32() != CATALOG_MAGIC, "Invalid native resource catalog.");
		const uint32_t count = file->get_32();
		ERR_FAIL_COND_MSG(count > file->get_length() / 24, "Invalid native resource catalog size.");
		for (uint32_t i = 0; i < count; i++) {
			const int64_t id = file->get_64();
			Entry entry;
			entry.target = file->get_64();
			entry.format = file->get_32();
			entry.type = file->get_pascal_string();
			const uint32_t variants = file->get_32();
			ERR_FAIL_COND_MSG(variants > file->get_length() / 12, "Invalid native resource variant count.");
			for (uint32_t j = 0; j < variants; j++) {
				const String feature = file->get_pascal_string();
				entry.variants.push_back({ feature, int64_t(file->get_64()) });
			}
			ERR_FAIL_COND_MSG(id <= 0 || entries.has(id) || file->get_error() != OK, "Corrupt native resource catalog entry.");
			entries.insert(id, entry);
		}
	}
};

const Entry *find(const String &p_path) {
	const int64_t id = from_path(p_path);
	if (id == 0) {
		return nullptr;
	}
	// C++ local static initialization is synchronized. The catalog is immutable
	// afterwards, including when Godot loads resource dependencies on workers.
	static const Catalog catalog;
	return catalog.entries.getptr(id);
}
} // namespace

String to_path(int64_t p_id) {
	return p_id == 0 ? String() : "res://" + itos(p_id);
}

int64_t from_path(const String &p_path) {
	if (!p_path.begins_with("res://") || p_path.length() <= 6 || p_path[6] < '1' || p_path[6] > '9') {
		return 0;
	}
	int64_t id = 0;
	for (int i = 6; i < p_path.length(); i++) {
		const char32_t digit = p_path[i];
		if (digit < '0' || digit > '9' || id > (INT64_MAX - (digit - '0')) / 10) {
			return 0;
		}
		id = id * 10 + digit - '0';
	}
	return id;
}

bool is_opaque(const String &p_path) {
	return from_path(p_path) != 0;
}

int64_t from_variant(const Variant &p_value) {
	if (p_value.get_type() == Variant::INT) {
		return p_value;
	}
	ERR_FAIL_COND_V_MSG(p_value.get_type() != Variant::STRING, 0, "Expected a serialized native resource identity.");
	const String path = p_value;
	const int64_t id = from_path(path);
	ERR_FAIL_COND_V_MSG(!path.is_empty() && id == 0, 0, "Serialized native resource identity is not opaque.");
	return id;
}

String remap(const String &p_path) {
	String path = p_path;
	while (const Entry *entry = find(path)) {
		int64_t target = entry->target;
		for (const Pair<String, int64_t> &variant : entry->variants) {
			if (OS::get_singleton()->has_feature(variant.first)) {
				target = variant.second;
				break;
			}
		}
		if (target == 0) {
			return path;
		}
		path = to_path(target);
	}
	return path;
}

StringName resource_type(const String &p_path) {
	const Entry *entry = find(p_path);
	return entry ? entry->type : StringName();
}

bool recognizes_extension(const String &p_path, const String &p_extension) {
	const Entry *entry = find(p_path);
	return entry && entry->format == p_extension.trim_prefix(".").to_lower().hash();
}
} // namespace WGodotResourcePaths
