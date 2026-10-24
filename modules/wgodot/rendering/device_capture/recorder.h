// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "encoder.h"
#include "core/io/file_access.h"
#include "core/os/condition_variable.h"
#include "core/os/thread.h"
#include "core/templates/list.h"

namespace WGPUCapture {
struct Statistics {
	uint64_t commands = 0;
	uint64_t raw_bytes = 0;
	uint64_t stored_bytes = 0;
	uint64_t peak_queue_bytes = 0;
	uint64_t queue_wait_usec = 0;
};

class Recorder {
	static Recorder *singleton;
	static thread_local uint32_t suspended;
	static thread_local uint32_t diagnostic;
	static constexpr uint64_t CHUNK_BYTES = 4 * 1024 * 1024;
	static constexpr uint64_t QUEUE_BYTES = 256 * 1024 * 1024;
	struct Chunk {
		Vector<uint8_t> bytes;
		uint64_t first = 0;
		uint32_t count = 0;
	};
	BinaryMutex mutex;
	ConditionVariable changed;
	List<Chunk> pending;
	Chunk current;
	uint64_t next_sequence = 1;
	uint64_t published_sequence = 0;
	uint64_t queued_bytes = 0;
	Statistics statistics;
	bool closing = false;
	SafeFlag sealed;
	Error failure = OK;
	String file_path;
	Ref<FileAccess> file;
	Thread writer;
	void queue_locked();
	static void writer_entry(void *p_data);
	void write_pending();
	int64_t begin_locked(Command p_command, const RenderingDevice *p_device, const MutexLock<BinaryMutex> &p_lock);
	uint64_t end_locked(int64_t p_offset);

public:
	static Error start(const String &p_directory);
	static void stop();
	static Error finish();
	static Recorder *get() { return suspended == 0 && singleton && !singleton->sealed.is_set() ? singleton : nullptr; }
	const String &get_path() const { return file_path; }
	Error flush();
	Error get_error();
	Statistics get_statistics();
	class Suspension {
	public:
		Suspension() { ++suspended; }
		~Suspension() { --suspended; }
	};
	class DiagnosticScope {
	public:
		DiagnosticScope() { ++diagnostic; }
		~DiagnosticScope() { --diagnostic; }
	};
	template <typename... T>
	static uint64_t record(const RenderingDevice *p_device, Command p_command, const T &...p_values) {
		Recorder *capture = get();
		if (!capture) {
			return 0;
		}
		MutexLock lock(capture->mutex);
		const int64_t offset = capture->begin_locked(p_command, p_device, lock);
		if (offset < 0) {
			return 0;
		}
		Encoder arguments(capture->current.bytes);
		arguments.u32(sizeof...(T));
		(arguments.put(p_values), ...);
		return capture->end_locked(offset);
	}
};
}

#define WGPU_RECORD(command, ...) \
	do { \
		if (WGPUCapture::Recorder::get()) { \
			WGPUCapture::Recorder::record(this, WGPUCapture::Command::command, __VA_ARGS__); \
		} \
	} while (false)
#define WGPU_RECORD_EMPTY(command) \
	do { \
		if (WGPUCapture::Recorder::get()) { \
			WGPUCapture::Recorder::record(this, WGPUCapture::Command::command); \
		} \
	} while (false)
#endif
