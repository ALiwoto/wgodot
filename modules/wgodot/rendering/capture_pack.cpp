// wgodot-changes::file
#include "capture_pack.h"
#include "capture_payload.h"

#ifdef TOOLS_ENABLED
#include "../wgodot_cli.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/marshalls.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"

namespace WCapturePack {
namespace {
constexpr uint64_t IMAGE_BATCH_BYTES = 256 * 1024 * 1024;

struct Entry {
	uint64_t id = 0;
	uint64_t size = 0;
	uint32_t count = 0;
	uint32_t raw_size = 0;
	uint32_t stride = 0;
	uint32_t block_bytes = 0;
	uint32_t kind = WCapturePayload::BYTES;
	bool sealed = false;
};

struct Archive {
	Ref<FileAccess> file;
	Vector<uint8_t> schema;
	bool journal = false;
	bool raw = false;
	bool legacy_payload = false;
	bool typed_payload = false;
	uint64_t next_sequence = 1;
	uint64_t length = 0;
	HashSet<uint64_t> identities;

	Error open(const String &p_path) {
		Error error = OK;
		file = FileAccess::open(p_path, FileAccess::READ, &error);
		if (error != OK) {
			return error;
		}
		// Sealed inputs are immutable. FileAccess::get_length() can seek to EOF
		// and back; doing that for every small tile dominates archive inspection.
		length = file->get_length();
		Vector<uint8_t> magic = file->get_buffer(8);
		if (magic.size() != 8) {
			return ERR_FILE_CORRUPT;
		}
		journal = memcmp(magic.ptr(), "WGPUJ001", 8) == 0 || memcmp(magic.ptr(), "WGPUJ002", 8) == 0;
		raw = memcmp(magic.ptr(), "WGPUJ002", 8) == 0 || memcmp(magic.ptr(), "WGPUP002", 8) == 0 || memcmp(magic.ptr(), "WGPUP004", 8) == 0;
		legacy_payload = memcmp(magic.ptr(), "WGPUP001", 8) == 0;
		typed_payload = memcmp(magic.ptr(), "WGPUP004", 8) == 0 || memcmp(magic.ptr(), "WGPUP005", 8) == 0;
		if (!journal && !raw && !legacy_payload && !typed_payload && memcmp(magic.ptr(), "WGPUP003", 8) != 0) {
			return ERR_FILE_UNRECOGNIZED;
		}
		if (journal) {
			uint32_t size = file->get_32();
			if (size > INT32_MAX || size > length - file->get_position()) {
				return ERR_FILE_CORRUPT;
			}
			schema = file->get_buffer(size);
			if (schema.size() != size) {
				return ERR_FILE_CORRUPT;
			}
		}
		return OK;
	}

	Error next(Entry &r_entry) {
		const uint64_t header_size = journal ? 20 : (legacy_payload ? 16 : (typed_payload ? 28 : 24));
		if (length - file->get_position() < header_size) {
			return ERR_FILE_CORRUPT;
		}
		uint8_t header[28];
		if (file->get_buffer(header, header_size) != header_size) {
			return ERR_FILE_CORRUPT;
		}
		r_entry = Entry();
		r_entry.id = decode_uint64(header);
		if (journal) {
			r_entry.count = decode_uint32(header + 8);
			r_entry.raw_size = decode_uint32(header + 12);
			const uint32_t stored = decode_uint32(header + 16);
			if (r_entry.id != next_sequence || (raw && stored != 0)) {
				return ERR_FILE_CORRUPT;
			}
			r_entry.size = raw ? r_entry.raw_size : stored;
			r_entry.sealed = r_entry.count == 0;
			next_sequence += r_entry.count;
			if ((r_entry.sealed && (stored || r_entry.raw_size)) || (!r_entry.sealed && !r_entry.raw_size)) {
				return ERR_FILE_CORRUPT;
			}
		} else {
			r_entry.size = decode_uint64(header + 8);
			if (!legacy_payload) {
				r_entry.stride = decode_uint32(header + 16);
				r_entry.block_bytes = decode_uint32(header + 20);
			}
			if (typed_payload) {
				r_entry.kind = decode_uint32(header + 24);
			}
			if (r_entry.kind > WCapturePayload::TILED_IMAGE || (r_entry.kind == WCapturePayload::TILED_IMAGE && (r_entry.stride || r_entry.block_bytes))) {
				return ERR_FILE_CORRUPT;
			}
			r_entry.sealed = r_entry.id == 0;
			if (r_entry.sealed && (r_entry.size || r_entry.stride || r_entry.block_bytes || r_entry.kind)) {
				return ERR_FILE_CORRUPT;
			}
			if ((r_entry.stride || r_entry.block_bytes) && (r_entry.stride != 8 || r_entry.block_bytes != BLOCK_BYTES || (raw && r_entry.size % 8))) {
				return ERR_FILE_CORRUPT;
			}
			if (!r_entry.sealed) {
				if (identities.has(r_entry.id)) {
					return ERR_FILE_CORRUPT;
				}
				identities.insert(r_entry.id);
			}
		}
		if (r_entry.sealed) {
			return file->get_position() == length ? OK : ERR_FILE_CORRUPT;
		}
		if (!r_entry.size || r_entry.size > INT32_MAX || r_entry.size > length - file->get_position()) {
			return ERR_FILE_CORRUPT;
		}
		return OK;
	}
};

struct ImageSource {
	Ref<FileAccess> file;
	int width = 0;
	int height = 0;
	Image::Format format = Image::FORMAT_L8;
	bool mipmaps = false;
	uint64_t size = 0;

	Error open(const String &p_path) {
		Error error = OK;
		file = FileAccess::open(p_path, FileAccess::READ, &error);
		if (error != OK) {
			return error;
		}
		Vector<uint8_t> magic = file->get_buffer(8);
		if (magic.size() != 8 || memcmp(magic.ptr(), "WGPUIMG1", 8) != 0 || file->get_length() < 32) {
			return ERR_FILE_CORRUPT;
		}
		width = file->get_32();
		height = file->get_32();
		uint32_t value = file->get_32();
		uint32_t mips = file->get_32();
		size = file->get_64();
		if (width <= 0 || width > Image::MAX_WIDTH || height <= 0 || height > Image::MAX_HEIGHT ||
				value >= Image::FORMAT_MAX || mips > 1 || !size || size > INT32_MAX || size != file->get_length() - 32) {
			return ERR_FILE_CORRUPT;
		}
		format = Image::Format(value);
		mipmaps = mips != 0;
		return Image::get_image_data_size(width, height, format, mipmaps) == int64_t(size) ? OK : ERR_FILE_CORRUPT;
	}

	uint64_t working_bytes() const {
		// Allow the source, decompressed mip chain and PNG encoder workspace.
		return size + uint64_t(width) * uint64_t(height) * 32;
	}
};

struct Progress {
	String path;
	String current;
	uint64_t raw_bytes = 0;
	uint64_t stored_bytes = 0;
	uint64_t next_write = 0;
	int completed = 0;
	int total = 0;

	Error write(const String &p_state, Error p_error = OK, bool p_force = false) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		if (!p_force && now < next_write) {
			return OK;
		}
		next_write = now + 1000000;
		ConfigFile status;
		status.set_value("pack", "state", p_state);
		status.set_value("pack", "current", current);
		status.set_value("pack", "completed", completed);
		status.set_value("pack", "total", total);
		status.set_value("pack", "raw_bytes", int64_t(raw_bytes));
		status.set_value("pack", "stored_bytes", int64_t(stored_bytes));
		status.set_value("pack", "error", int(p_error));
		const String temporary = path + ".tmp";
		Error error = status.save(temporary);
		return error == OK ? DirAccess::rename_absolute(temporary, path) : error;
	}
};

Error check_archive(const String &p_path) {
	Archive source;
	Error error = source.open(p_path);
	while (error == OK) {
		Entry entry;
		error = source.next(entry);
		if (error != OK || entry.sealed) {
			return error;
		}
		source.file->seek(source.file->get_position() + entry.size);
	}
	return error;
}

Error pack_archive(const String &p_path, Progress &r_progress) {
	Archive source;
	Error error = source.open(p_path);
	if (error != OK || !source.raw) {
		return error;
	}
	const String temporary = p_path + ".packing";
	Ref<FileAccess> output = FileAccess::open(temporary, FileAccess::WRITE, &error);
	if (error != OK) {
		return error;
	}
	bool stored = output->store_buffer(reinterpret_cast<const uint8_t *>(source.journal ? "WGPUJ001" : (source.typed_payload ? "WGPUP005" : "WGPUP003")), 8);
	if (stored && source.journal) {
		stored = output->store_32(source.schema.size()) && output->store_buffer(source.schema);
	}
	if (!stored) {
		error = ERR_FILE_CANT_WRITE;
	}
	while (error == OK) {
		// A tile is small; batching entries gives the CPU pool enough independent
		// blocks instead of dispatching and waiting for one tiny job at a time.
		Vector<Entry> entries;
		Vector<WReadbackCodec::Input> inputs;
		uint64_t batch_bytes = 0;
		bool sealed = false;
		while (error == OK && !sealed && batch_bytes < 16 * 1024 * 1024) {
			Entry entry;
			error = source.next(entry);
			if (error != OK) {
				break;
			}
			entries.push_back(entry);
			sealed = entry.sealed;
			WReadbackCodec::Input input;
			input.bytes = source.file->get_buffer(entry.size);
			input.shuffle_stride = entry.stride;
			if (input.bytes.size() != int64_t(entry.size)) {
				error = ERR_FILE_CORRUPT;
				break;
			}
			inputs.push_back(input);
			batch_bytes += entry.size;
		}
		if (error != OK) {
			break;
		}
		Vector<WReadbackCodec::Output> results = WReadbackCodec::encode(inputs);
		if (results.size() != entries.size()) {
			error = ERR_CANT_CREATE;
			break;
		}
		for (int i = 0; i < entries.size(); i++) {
			const Entry &entry = entries[i];
			const WReadbackCodec::Output &encoded = results[i];
			if (encoded.error != OK) {
				error = encoded.error;
				break;
			}
			stored = output->store_64(entry.id);
			if (source.journal) {
				stored = stored && output->store_32(entry.count) && output->store_32(entry.raw_size) && output->store_32(encoded.bytes.size());
			} else {
				stored = stored && output->store_64(encoded.bytes.size()) && output->store_32(entry.stride) && output->store_32(entry.block_bytes);
				if (source.typed_payload) {
					stored = stored && output->store_32(entry.kind);
				}
			}
			stored = stored && output->store_buffer(encoded.bytes);
			if (!stored) {
				error = ERR_FILE_CANT_WRITE;
				break;
			}
			r_progress.raw_bytes += entry.size;
			r_progress.stored_bytes += encoded.bytes.size();
		}
		if (error == OK) {
			error = r_progress.write("compressing");
		}
		if (sealed) {
			break;
		}
	}
	output->flush();
	if (error == OK) {
		error = output->get_error();
	}
	output.unref();
	source.file.unref();
	if (error == OK) {
		error = DirAccess::rename_absolute(temporary, p_path);
	}
	if (error != OK) {
		DirAccess::remove_absolute(temporary);
	}
	return error;
}

struct ImageJob {
	String path;
	Error error = OK;
	uint64_t raw_bytes = 0;
	uint64_t stored_bytes = 0;

	Error store(const String &p_path, const Vector<uint8_t> &p_bytes) {
		const String temporary = p_path + ".packing";
		Error result = OK;
		Ref<FileAccess> output = FileAccess::open(temporary, FileAccess::WRITE, &result);
		if (result != OK) {
			return result;
		}
		if (!output->store_buffer(p_bytes)) {
			result = ERR_FILE_CANT_WRITE;
		}
		output->flush();
		if (result == OK) {
			result = output->get_error();
		}
		output.unref();
		if (result == OK) {
			result = DirAccess::rename_absolute(temporary, p_path);
		}
		if (result != OK) {
			DirAccess::remove_absolute(temporary);
		} else {
			stored_bytes += p_bytes.size();
		}
		return result;
	}

	void pack() {
		ImageSource source;
		error = source.open(path);
		if (error != OK) {
			return;
		}
		Vector<uint8_t> data = source.file->get_buffer(source.size);
		if (data.size() != int64_t(source.size)) {
			error = ERR_FILE_CORRUPT;
			return;
		}
		source.file.unref();
		raw_bytes = source.size;
		Ref<Image> image = Image::create_from_data(source.width, source.height, source.mipmaps, source.format, data);
		data.clear();
		if (image.is_null() || image->is_empty()) {
			error = ERR_FILE_CORRUPT;
			return;
		}
		const String base = path.get_basename();
		if (image->is_compressed()) {
			error = store(base + ".bin", image->get_data());
			if (error == OK) {
				error = image->decompress();
			}
		}
		for (int mip = 0; error == OK && mip <= image->get_mipmap_count(); mip++) {
			Ref<Image> level = image->get_image_from_mipmap(mip);
			Vector<uint8_t> png = level->save_png_to_buffer();
			if (png.is_empty()) {
				error = ERR_CANT_CREATE;
				break;
			}
			error = store(base + (mip ? "-" + itos(mip) : String()) + ".png", png);
		}
		if (error == OK) {
			error = DirAccess::remove_absolute(path);
		}
	}

	static void task(void *p_data, uint32_t p_index) {
		static_cast<ImageJob *>(p_data)[p_index].pack();
	}
};

Error execute(const Vector<String> &p_files, Progress &r_progress) {
	// Validate every seal before starting any compression. An unfinished file
	// means the capture session has not drained, even if its UI says otherwise.
	for (const String &path : p_files) {
		r_progress.current = path;
		Error error;
		if (path.get_extension() == "wgi") {
			ImageSource source;
			error = source.open(path);
		} else {
			error = check_archive(path);
		}
		if (error != OK) {
			return error;
		}
		error = r_progress.write("checking_seals");
		if (error != OK) {
			return error;
		}
	}
	for (int i = 0; i < p_files.size();) {
		r_progress.current = p_files[i];
		if (p_files[i].get_extension() != "wgi") {
			Error error = pack_archive(p_files[i++], r_progress);
			if (error != OK) {
				return error;
			}
			r_progress.completed++;
			continue;
		}
		Vector<ImageJob> jobs;
		uint64_t working = 0;
		while (i < p_files.size() && p_files[i].get_extension() == "wgi") {
			ImageSource source;
			Error error = source.open(p_files[i]);
			if (error != OK) {
				return error;
			}
			if (!jobs.is_empty() && working + source.working_bytes() > IMAGE_BATCH_BYTES) {
				break;
			}
			working += source.working_bytes();
			ImageJob job;
			job.path = p_files[i++];
			jobs.push_back(job);
		}
		if (!jobs.is_empty()) {
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			auto task = pool->add_native_group_task(&ImageJob::task, jobs.ptrw(), jobs.size(), -1, true, "Pack captured textures");
			pool->wait_for_group_task_completion(task);
		}
		for (const ImageJob &job : jobs) {
			if (job.error != OK) {
				r_progress.current = job.path;
				return job.error;
			}
			r_progress.raw_bytes += job.raw_bytes;
			r_progress.stored_bytes += job.stored_bytes;
			r_progress.completed++;
		}
		Error error = r_progress.write("compressing");
		if (error != OK) {
			return error;
		}
	}
	return OK;
}
} // namespace

Error store_image(const Ref<Image> &p_image, const String &p_path) {
	ERR_FAIL_COND_V(p_image.is_null() || p_image->is_empty(), ERR_INVALID_PARAMETER);
	Error error = OK;
	Ref<FileAccess> output = FileAccess::open(p_path, FileAccess::WRITE, &error);
	if (error != OK) {
		return error;
	}
	const Vector<uint8_t> data = p_image->get_data();
	if (!output->store_buffer(reinterpret_cast<const uint8_t *>("WGPUIMG1"), 8) ||
			!output->store_32(p_image->get_width()) || !output->store_32(p_image->get_height()) ||
			!output->store_32(p_image->get_format()) || !output->store_32(p_image->has_mipmaps()) ||
			!output->store_64(data.size()) || !output->store_buffer(data)) {
		return ERR_FILE_CANT_WRITE;
	}
	output->flush();
	return output->get_error();
}

int run(const Vector<String> &p_arguments) {
	String list_path;
	Progress progress;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String option = p_arguments[i];
		if ((option != "--files" && option != "--status") || i + 1 == p_arguments.size()) {
			print_error("Usage: wg capture-pack --files <file-list> --status <status.ini>");
			return 2;
		}
		String path = WGodotCLI::make_absolute_output_path(p_arguments[++i]);
		if (option == "--files") {
			list_path = path;
		} else {
			progress.path = path;
		}
	}
	if (list_path.is_empty() || progress.path.is_empty()) {
		print_error("capture-pack requires --files and --status.");
		return 2;
	}
	Error error = OK;
	Ref<FileAccess> list = FileAccess::open(list_path, FileAccess::READ, &error);
	if (error != OK) {
		print_error("Cannot read capture file list: " + list_path);
		return 1;
	}
	Vector<String> files;
	HashSet<String> seen;
	while (list->get_position() < list->get_length()) {
		String path = list->get_line();
		if (path.is_empty()) {
			continue;
		}
		if (!path.is_absolute_path()) {
			print_error("Capture file list requires absolute paths: " + path);
			return 2;
		}
		if (!seen.has(path)) {
			seen.insert(path);
			files.push_back(path);
		}
	}
	progress.total = files.size();
	error = progress.write("checking_seals", OK, true);
	if (error == OK) {
		error = execute(files, progress);
	}
	Error status_error = progress.write(error == OK ? "succeeded" : "failed", error, true);
	if (error == OK) {
		error = status_error;
	}
	if (error != OK) {
		print_error(vformat("Capture packing failed: %s (%s). Raw input retained for retry.", progress.current, error_names[error]));
		return 1;
	}
	print_line(vformat("Capture packing succeeded: %d files, %d raw bytes -> %d stored bytes.", progress.completed, progress.raw_bytes, progress.stored_bytes));
	return 0;
}
} // namespace WCapturePack
#endif
