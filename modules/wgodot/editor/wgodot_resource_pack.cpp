// wgodot-changes::file
#include "wgodot_resource_pack.h"

#include "core/io/file_access_pack.h"
#include "core/io/pck_packer.h"

Error WGodotResourcePack::open(const String &p_path) {
	Error error;
	source = FileAccess::open(p_path, FileAccess::READ, &error);
	RETURN_IF_ERROR(error);
	ERR_FAIL_COND_V(source->get_32() != PACK_HEADER_MAGIC, ERR_FILE_UNRECOGNIZED);
	const uint32_t version = source->get_32();
	ERR_FAIL_COND_V(version != PACK_FORMAT_VERSION_V3 && version != PACK_FORMAT_VERSION_V4, ERR_FILE_UNRECOGNIZED);
	source->seek(20);
	ERR_FAIL_COND_V(source->get_32() != PACK_REL_FILEBASE, ERR_UNAVAILABLE);
	const uint64_t base = source->get_64();
	const uint64_t directory = source->get_64();
	ERR_FAIL_COND_V(directory >= source->get_length(), ERR_FILE_CORRUPT);
	source->seek(directory);
	const uint32_t count = source->get_32();
	ERR_FAIL_COND_V(count > (source->get_length() - directory) / 40, ERR_FILE_CORRUPT);
	HashSet<String> names;
	for (uint32_t i = 0; i < count; i++) {
		Entry entry;
		const uint32_t length = source->get_32();
		ERR_FAIL_COND_V(length > source->get_length() - source->get_position(), ERR_FILE_CORRUPT);
		Vector<uint8_t> name;
		name.resize(length + 1);
		ERR_FAIL_COND_V(source->get_buffer(name.ptrw(), length) != length, ERR_FILE_CORRUPT);
		name.write[length] = 0;
		entry.path = "res://" + String::utf8(reinterpret_cast<const char *>(name.ptr()));
		const uint64_t relative = source->get_64();
		ERR_FAIL_COND_V(base > directory || relative > directory - base, ERR_FILE_CORRUPT);
		entry.offset = base + relative;
		entry.size = source->get_64();
		ERR_FAIL_COND_V(entry.size > directory - entry.offset, ERR_FILE_CORRUPT);
		entry.md5.resize(16);
		ERR_FAIL_COND_V(source->get_buffer(entry.md5.ptrw(), 16) != 16, ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V_MSG(source->get_32() != 0, ERR_UNAVAILABLE, "Data-only updates require a complete, unencrypted pack.");
		ERR_FAIL_COND_V(names.has(entry.path), ERR_FILE_CORRUPT);
		names.insert(entry.path);
		entries.push_back(entry);
	}
	return source->get_error();
}

PackedByteArray WGodotResourcePack::read(const String &p_path) const {
	for (const Entry &entry : entries) {
		if (entry.path == p_path) {
			source->seek(entry.offset);
			return source->get_buffer(entry.size);
		}
	}
	return {};
}

Error WGodotResourcePack::rewrite(const String &p_destination, const HashMap<String, String> &p_replacements, const String &p_identity_path, const PackedByteArray &p_identity) {
	Ref<PCKPacker> packer;
	packer.instantiate();
	RETURN_IF_ERROR(packer->pck_start(p_destination));
	Vector<uint8_t> buffer;
	buffer.resize(1024 * 1024);
	int replaced = 0;
	bool identity_found = false;
	for (const Entry &entry : entries) {
		if (entry.path == p_identity_path) {
			RETURN_IF_ERROR(packer->add_file_from_buffer(entry.path, p_identity));
			identity_found = true;
		} else if (const String *replacement = p_replacements.getptr(entry.path)) {
			RETURN_IF_ERROR(packer->add_file(entry.path, *replacement));
			replaced++;
		} else {
			RETURN_IF_ERROR(packer->copy_entry(entry.path, source, entry.offset, entry.size, entry.md5, buffer));
		}
	}
	ERR_FAIL_COND_V_MSG(replaced != p_replacements.size() || !identity_found, ERR_INVALID_DATA, "Resource payload or generation identity is missing from the pack; run normal packaging.");
	return packer->flush();
}
