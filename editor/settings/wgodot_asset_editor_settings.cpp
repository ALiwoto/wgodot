// wgodot-changes::file
#include "wgodot_asset_editor_settings.h"

#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/math/math_funcs_binary.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"
#include "editor/file_system/editor_paths.h"

namespace {
constexpr uint32_t MAGIC = 0x57474150; // WGAP
constexpr uint32_t VERSION = 1;
constexpr uint32_t BUCKETS = 65536;
constexpr uint64_t HEADER_SIZE = 16;
constexpr uint64_t DATA_START = HEADER_SIZE + BUCKETS * sizeof(uint64_t);
constexpr uint64_t RECORD_HEADER_SIZE = 24;
constexpr uint32_t MAX_PAYLOAD_SIZE = 1024 * 1024;
constexpr uint32_t MAX_KEY_SIZE = 65536;

struct Record {
	uint64_t offset = 0;
	uint64_t link_offset = 0;
	uint64_t next = 0;
	uint32_t key_size = 0;
	uint32_t capacity = 0;
	uint32_t size = 0;
	uint32_t checksum = 0;
};

Ref<FileAccess> open_store(bool p_create, Error &r_error) {
	const String path = EditorPaths::get_singleton()->get_project_data_dir().path_join("assets_editor_personal_params.dat");
	const bool exists = FileAccess::exists(path);
	if (!exists && !p_create) {
		r_error = ERR_FILE_NOT_FOUND;
		return Ref<FileAccess>();
	}
	Ref<FileAccess> file = FileAccess::open(path, exists ? (p_create ? FileAccess::READ_WRITE : FileAccess::READ) : FileAccess::WRITE_READ, &r_error);
	if (file.is_null()) {
		return file;
	}
	if (!exists) {
		Vector<uint8_t> directory;
		directory.resize(DATA_START);
		directory.fill(0);
		if (!file->store_buffer(directory)) {
			r_error = ERR_FILE_CANT_WRITE;
			return Ref<FileAccess>();
		}
		file->seek(0);
		if (!file->store_32(MAGIC) || !file->store_32(VERSION) || !file->store_32(BUCKETS) || !file->store_32(0)) {
			r_error = ERR_FILE_CANT_WRITE;
			return Ref<FileAccess>();
		}
	}
	file->seek(0);
	if (file->get_length() < DATA_START || file->get_32() != MAGIC || file->get_32() != VERSION || file->get_32() != BUCKETS) {
		r_error = ERR_FILE_CORRUPT;
		return Ref<FileAccess>();
	}
	r_error = OK;
	return file;
}

Error find_record(const Ref<FileAccess> &p_file, const String &p_key, Record &r_record) {
	// Bucket heads and record links are byte offsets. No asset-wide index scan.
	r_record.link_offset = HEADER_SIZE + (p_key.hash64() % BUCKETS) * sizeof(uint64_t);
	p_file->seek(r_record.link_offset);
	uint64_t offset = p_file->get_64();
	const uint64_t length = p_file->get_length();
	HashSet<uint64_t> visited;
	while (offset) {
		if (offset < DATA_START || offset > length || length - offset < RECORD_HEADER_SIZE || visited.has(offset)) {
			return ERR_FILE_CORRUPT;
		}
		visited.insert(offset);
		p_file->seek(offset);
		r_record.next = p_file->get_64();
		r_record.key_size = p_file->get_32();
		r_record.capacity = p_file->get_32();
		r_record.size = p_file->get_32();
		r_record.checksum = p_file->get_32();
		if (r_record.key_size > MAX_KEY_SIZE || r_record.capacity > MAX_PAYLOAD_SIZE || r_record.size > r_record.capacity ||
				uint64_t(r_record.key_size) + r_record.capacity > length - offset - RECORD_HEADER_SIZE) {
			return ERR_FILE_CORRUPT;
		}
		const Vector<uint8_t> key = p_file->get_buffer(r_record.key_size);
		if (key.size() != r_record.key_size) {
			return ERR_FILE_CORRUPT;
		}
		if (String::utf8(reinterpret_cast<const char *>(key.ptr()), key.size()) == p_key) {
			r_record.offset = offset;
			return OK;
		}
		r_record.link_offset = offset;
		offset = r_record.next;
	}
	r_record.next = 0;
	return ERR_DOES_NOT_EXIST;
}
} // namespace

Error WGodotAssetEditorSettings::load(const String &p_key, Dictionary &r_settings) {
	r_settings.clear();
	Error error;
	const Ref<FileAccess> file = open_store(false, error);
	if (file.is_null()) {
		return error;
	}
	Record record;
	error = find_record(file, p_key, record);
	if (error != OK) {
		return error;
	}
	file->seek(record.offset + RECORD_HEADER_SIZE + record.key_size);
	const Vector<uint8_t> bytes = file->get_buffer(record.size);
	if (bytes.size() != record.size || hash_djb2_buffer(bytes.ptr(), bytes.size()) != record.checksum) {
		return ERR_FILE_CORRUPT;
	}
	Variant value;
	error = decode_variant(value, bytes.ptr(), bytes.size());
	if (error != OK || value.get_type() != Variant::DICTIONARY) {
		return ERR_FILE_CORRUPT;
	}
	r_settings = value;
	return OK;
}

Error WGodotAssetEditorSettings::save(const String &p_key, const Dictionary &p_settings) {
	const CharString key = p_key.utf8();
	int size = 0;
	Error error = encode_variant(p_settings, nullptr, size);
	if (error != OK) {
		return error;
	}
	if (size > int(MAX_PAYLOAD_SIZE) || key.length() > int(MAX_KEY_SIZE)) {
		return ERR_OUT_OF_MEMORY;
	}
	Vector<uint8_t> bytes;
	bytes.resize(size);
	error = encode_variant(p_settings, bytes.ptrw(), size);
	if (error != OK) {
		return error;
	}
	const uint32_t checksum = hash_djb2_buffer(bytes.ptr(), bytes.size());
	const Ref<FileAccess> file = open_store(true, error);
	if (file.is_null()) {
		return error;
	}
	Record record;
	error = find_record(file, p_key, record);
	if (error != OK && error != ERR_DOES_NOT_EXIST) {
		return error;
	}
	if (record.offset && size <= int(record.capacity)) {
		// Reuse the allocation: camera movement must not grow the file indefinitely.
		file->seek(record.offset + RECORD_HEADER_SIZE + record.key_size);
		if (!file->store_buffer(bytes)) {
			return ERR_FILE_CANT_WRITE;
		}
		file->seek(record.offset + 16);
		if (!file->store_32(size) || !file->store_32(checksum)) {
			return ERR_FILE_CANT_WRITE;
		}
	} else {
		// Only new assets or larger settings need a new allocation.
		const uint32_t capacity = Math::next_power_of_2(uint32_t(MAX(size, 1024)));
		const uint64_t offset = file->get_length();
		error = file->resize(offset + RECORD_HEADER_SIZE + key.length() + capacity);
		if (error != OK) {
			return error;
		}
		file->seek(offset);
		if (!file->store_64(record.next) || !file->store_32(key.length()) || !file->store_32(capacity) ||
				!file->store_32(size) || !file->store_32(checksum) ||
				!file->store_buffer(reinterpret_cast<const uint8_t *>(key.get_data()), key.length()) || !file->store_buffer(bytes)) {
			return ERR_FILE_CANT_WRITE;
		}
		// Publish the record after its contents, preserving the rest of the bucket.
		file->seek(record.link_offset);
		if (!file->store_64(offset)) {
			return ERR_FILE_CANT_WRITE;
		}
	}
	file->flush();
	return file->get_error();
}
