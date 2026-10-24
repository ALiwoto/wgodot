// wgodot-changes::file
#include "recorder.h"
#include "../gpu_profile.h"

#ifdef TOOLS_ENABLED
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/version.h"
#include "core/os/time.h"

namespace WGPUCapture {
Recorder *Recorder::singleton = nullptr;
thread_local uint32_t Recorder::suspended = 0;
thread_local uint32_t Recorder::diagnostic = 0;

Error Recorder::start(const String &p_directory) {
	ERR_FAIL_COND_V(singleton, ERR_ALREADY_IN_USE);
	Error error = DirAccess::make_dir_recursive_absolute(p_directory);
	ERR_FAIL_COND_V(error != OK, error);
	Recorder *capture = memnew(Recorder);
	capture->file_path = p_directory.path_join(vformat("device_%d_%.6f.wgc", OS::get_singleton()->get_process_id(), Time::get_singleton()->get_unix_time_from_system()));
	capture->file = FileAccess::open(capture->file_path, FileAccess::WRITE, &error);
	if (error != OK) {
		memdelete(capture);
		return error;
	}
	// The schema travels with the journal. Inspection does not depend on an
	// installed engine, addresses, C++ struct padding or a matching executable.
	Encoder schema;
	schema.put(String(GODOT_VERSION_FULL_CONFIG));
	schema.u32(uint32_t(Command::COUNT));
#define GPU_COMMAND(id, name, fields) schema.put(name); schema.put(fields);
#include "commands.inc"
#undef GPU_COMMAND
	schema.u32(uint32_t(Structure::COUNT));
#define GPU_STRUCTURE(id, fields) schema.put(#id); schema.put(fields);
#include "structures.inc"
#undef GPU_STRUCTURE
	capture->file->store_buffer(reinterpret_cast<const uint8_t *>("WGPUJ002"), 8);
	capture->file->store_32(schema.bytes.size());
	capture->file->store_buffer(schema.bytes);
	capture->file->flush();
	if (capture->file->get_error() != OK) {
		error = capture->file->get_error();
		memdelete(capture);
		return error;
	}
#ifdef THREADS_ENABLED
	const Thread::ID thread = capture->writer.start(&Recorder::writer_entry, capture);
	if (thread == Thread::UNASSIGNED_ID) {
		memdelete(capture);
		return ERR_CANT_CREATE;
	}
#else
	memdelete(capture);
	return ERR_UNAVAILABLE;
#endif
	singleton = capture;
	return OK;
}

void Recorder::queue_locked() {
	if (current.count == 0) {
		return;
	}
	queued_bytes += current.bytes.size();
	statistics.peak_queue_bytes = MAX(statistics.peak_queue_bytes, queued_bytes);
	pending.push_back(std::move(current));
	current = Chunk();
	changed.notify_all();
}

int64_t Recorder::begin_locked(Command p_command, const RenderingDevice *p_device, const MutexLock<BinaryMutex> &p_lock) {
	const uint64_t wait_started = queued_bytes >= QUEUE_BYTES ? OS::get_singleton()->get_ticks_usec() : 0;
	while (queued_bytes >= QUEUE_BYTES && failure == OK && !sealed.is_set()) {
		changed.wait(p_lock);
	}
	if (wait_started) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		statistics.queue_wait_usec += now - wait_started;
		WGodotGPUProfile::interval("journal.queue_backpressure", wait_started, now);
	}
	if (failure != OK || closing || sealed.is_set()) {
		return -1;
	}
	const uint64_t sequence = next_sequence++;
	if (current.count == 0) {
		current.first = sequence;
	}
	const int64_t offset = current.bytes.size();
	current.bytes.resize(offset + 28);
	uint8_t *destination = current.bytes.ptrw() + offset;
	encode_uint32(uint32_t(p_command), destination);
	encode_uint32(0, destination + 4);
	encode_uint64(uint64_t(reinterpret_cast<uintptr_t>(p_device)), destination + 8);
	encode_uint64(Thread::get_caller_id(), destination + 16);
	encode_uint32(diagnostic ? 1 : 0, destination + 24);
	return offset;
}

uint64_t Recorder::end_locked(int64_t p_offset) {
	const int64_t size = current.bytes.size() - p_offset - 28;
	if (size > UINT32_MAX || current.bytes.size() > UINT32_MAX) {
		failure = ERR_PARAMETER_RANGE_ERROR;
		changed.notify_all();
		return 0;
	}
	encode_uint32(uint32_t(size), current.bytes.ptrw() + p_offset + 4);
	current.count++;
	statistics.commands++;
	statistics.raw_bytes += 28 + size;
	if (current.bytes.size() >= CHUNK_BYTES) {
		queue_locked();
	}
	return next_sequence - 1;
}

void Recorder::writer_entry(void *p_data) {
	Thread::set_name("GPU capture writer");
	static_cast<Recorder *>(p_data)->write_pending();
}

void Recorder::write_pending() {
	while (true) {
		Chunk chunk;
		{
			MutexLock lock(mutex);
			while (pending.is_empty() && !closing) {
				changed.wait(lock);
			}
			if (pending.is_empty()) {
				return;
			}
			chunk = std::move(pending.front()->get());
			pending.pop_front();
		}
		// Raw sequential spool. Compression is a separate post-session command.
		// stored_size=0 identifies raw chunks in the v2 journal.
		WGodotGPUProfile::Scope writing("journal.write_chunk");
		const bool stored = file->store_64(chunk.first) && file->store_32(chunk.count) &&
				file->store_32(chunk.bytes.size()) && file->store_32(0) &&
				file->store_buffer(chunk.bytes);
		const Error error = stored ? file->get_error() : ERR_FILE_CANT_WRITE;
		writing.finish();
		{
			MutexLock lock(mutex);
			queued_bytes -= chunk.bytes.size();
			if (error != OK) {
				failure = error;
				pending.clear();
				current = Chunk();
				queued_bytes = 0;
			} else {
				published_sequence = chunk.first + chunk.count - 1;
				statistics.stored_bytes += chunk.bytes.size();
			}
			changed.notify_all();
		}
		if (error != OK) {
			return;
		}
	}
}

Error Recorder::flush() {
	MutexLock lock(mutex);
	queue_locked();
	const uint64_t last = next_sequence - 1;
	while (published_sequence < last && failure == OK) {
		changed.wait(lock);
	}
	return failure;
}

Error Recorder::get_error() {
	MutexLock lock(mutex);
	return failure;
}

Statistics Recorder::get_statistics() {
	MutexLock lock(mutex);
	return statistics;
}

Error Recorder::finish() {
	if (!singleton) {
		return ERR_UNCONFIGURED;
	}
	Recorder *capture = singleton;
	{
		MutexLock lock(capture->mutex);
		if (capture->sealed.is_set()) {
			return ERR_ALREADY_IN_USE;
		}
		capture->sealed.set();
		capture->changed.notify_all();
	}
	const Error error = capture->flush();
	{
		MutexLock lock(capture->mutex);
		capture->closing = true;
		capture->changed.notify_all();
	}
	capture->writer.wait_to_finish();
	if (error != OK) {
		return error;
	}
	// An empty chunk seals the stream. No producers can append beyond it;
	// comparison can retain stable file stamps while the lab stays open.
	capture->file->store_64(capture->next_sequence);
	capture->file->store_32(0);
	capture->file->store_32(0);
	capture->file->store_32(0);
	capture->file->flush();
	const Error file_error = capture->file->get_error();
	// Publish final filesystem metadata as well as the seal. No writer or
	// reader needs this handle to stay open after recording has finished.
	capture->file.unref();
	return file_error;
}

void Recorder::stop() {
	if (!singleton) {
		return;
	}
	// Called after RenderingDevice and its producers have stopped.
	Recorder *capture = singleton;
	if (!capture->sealed.is_set()) {
		finish();
	}
	singleton = nullptr;
	capture->flush();
	{
		MutexLock lock(capture->mutex);
		capture->closing = true;
		capture->changed.notify_all();
	}
	if (capture->writer.is_started()) {
		capture->writer.wait_to_finish();
	}
	memdelete(capture);
}

}
#endif
