// wgodot-changes::file
#pragma once

#include "core/templates/local_vector.h"
#include "core/object/worker_thread_pool.h"
#include "servers/rendering/rendering_device_driver.h"

// A completed download can contain many regions in the same staging buffer.
// Keep its mapping until the CPU copy is complete, then release every mapping
// before invoking the caller's callback (which may submit more device work).
class RenderingDeviceReadbackMappings {
	struct Mapping {
		RenderingDeviceDriver::BufferID buffer;
		uint8_t *data = nullptr;
	};
	RenderingDeviceDriver *driver;
	LocalVector<Mapping> mappings;

public:
	explicit RenderingDeviceReadbackMappings(RenderingDeviceDriver *p_driver) :
			driver(p_driver) {}
	RenderingDeviceReadbackMappings(const RenderingDeviceReadbackMappings &) = delete;
	RenderingDeviceReadbackMappings &operator=(const RenderingDeviceReadbackMappings &) = delete;

	uint8_t *get(RenderingDeviceDriver::BufferID p_buffer) {
		for (const Mapping &mapping : mappings) {
			if (mapping.buffer == p_buffer) {
				return mapping.data;
			}
		}
		uint8_t *data = driver->buffer_map(p_buffer);
		if (data) {
			mappings.push_back({ p_buffer, data });
		}
		return data;
	}

	~RenderingDeviceReadbackMappings() {
		for (const Mapping &mapping : mappings) {
			driver->buffer_unmap(mapping.buffer);
		}
	}
};

// Only CPU copies enter the pool. The render thread owns mapping/unmapping,
// waits for every copy, and then invokes callbacks in their original order.
class RenderingDeviceReadbackCopies {
	struct Copy {
		uint8_t *destination;
		const uint8_t *source;
		uint64_t row_bytes;
		uint64_t destination_pitch;
		uint64_t source_pitch;
		uint32_t rows;
	};
	LocalVector<Copy> copies;
	uint64_t byte_count = 0;
	static constexpr uint64_t COPY_BYTES = 1024 * 1024;

	static void execute(void *p_data, uint32_t p_index) {
		const Copy &copy = static_cast<RenderingDeviceReadbackCopies *>(p_data)->copies[p_index];
		if (copy.row_bytes == copy.destination_pitch && copy.row_bytes == copy.source_pitch) {
			memcpy(copy.destination, copy.source, copy.row_bytes * copy.rows);
		} else {
			for (uint32_t row = 0; row < copy.rows; row++) {
				memcpy(copy.destination + row * copy.destination_pitch, copy.source + row * copy.source_pitch, copy.row_bytes);
			}
		}
	}

public:
	void add(uint8_t *p_destination, const uint8_t *p_source, uint64_t p_row_bytes, uint64_t p_destination_pitch, uint64_t p_source_pitch, uint32_t p_rows) {
		if (p_row_bytes == p_destination_pitch && p_row_bytes == p_source_pitch) {
			uint64_t remaining = p_row_bytes * p_rows;
			while (remaining) {
				const uint64_t size = MIN(remaining, COPY_BYTES);
				copies.push_back({ p_destination, p_source, size, size, size, 1 });
				byte_count += size;
				p_destination += size;
				p_source += size;
				remaining -= size;
			}
		} else {
			const uint32_t batch_rows = uint32_t(MAX(uint64_t(1), COPY_BYTES / p_row_bytes));
			while (p_rows) {
				const uint32_t rows = MIN(p_rows, batch_rows);
				copies.push_back({ p_destination, p_source, p_row_bytes, p_destination_pitch, p_source_pitch, rows });
				byte_count += p_row_bytes * rows;
				p_destination += p_destination_pitch * rows;
				p_source += p_source_pitch * rows;
				p_rows -= rows;
			}
		}
	}

	void finish() {
		if (copies.size() > 1 && byte_count >= COPY_BYTES) {
			WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
			const auto task = pool->add_native_group_task(execute, this, copies.size(), -1, true, "Copy GPU readbacks");
			pool->wait_for_group_task_completion(task);
		} else {
			for (uint32_t i = 0; i < copies.size(); i++) {
				execute(this, i);
			}
		}
	}
};
