// wgodot-changes::file
#include "capture_payload.h"
#ifdef TOOLS_ENABLED
#include "readback_codec.h"
#include "core/io/marshalls.h"

namespace WCapturePayload {
Tiles compare_tiles(const Vector<uint8_t> &p_bytes, uint32_t p_width, uint32_t p_height, uint32_t p_pixel_bytes,
		const Vector<uint8_t> &p_previous_bytes, const Tiles &p_previous) {
	Tiles result;
	if (!p_pixel_bytes || uint64_t(p_width) * p_height * p_pixel_bytes != uint64_t(p_bytes.size())) {
		return result; // Non-image/mip-chain payloads retain ordinary byte storage.
	}
	result.width = p_width;
	result.height = p_height;
	result.pixel_bytes = p_pixel_bytes;
	const uint32_t columns = (p_width + TILE_SIZE - 1) / TILE_SIZE;
	const uint32_t rows = (p_height + TILE_SIZE - 1) / TILE_SIZE;
	result.entries.resize(columns * rows);
	uint64_t *entries = result.entries.ptrw();
	const bool comparable = p_previous.width == p_width && p_previous.height == p_height &&
			p_previous.pixel_bytes == p_pixel_bytes && p_previous_bytes.size() == p_bytes.size() &&
			p_previous.entries.size() == int64_t(columns * rows);
	const uint64_t pitch = uint64_t(p_width) * p_pixel_bytes;
	const uint8_t *bytes = p_bytes.ptr();
	const uint8_t *previous_bytes = p_previous_bytes.ptr();
	const uint64_t *previous_entries = p_previous.entries.ptr();
	for (uint32_t y = 0, index = 0; y < p_height; y += TILE_SIZE) {
		for (uint32_t x = 0; x < p_width; x += TILE_SIZE, index++) {
			const uint32_t width = MIN(TILE_SIZE, p_width - x);
			const uint32_t height = MIN(TILE_SIZE, p_height - y);
			bool same = comparable;
			uint64_t offset = uint64_t(y) * pitch + uint64_t(x) * p_pixel_bytes;
			for (uint32_t row = 0; same && row < height; row++) {
				same = memcmp(bytes + offset, previous_bytes + offset, width * p_pixel_bytes) == 0;
				offset += pitch;
			}
			if (same) {
				entries[index] = previous_entries[index];
			} else {
				entries[index] = 0;
				result.changed.push_back(index);
			}
		}
	}
	return result;
}

Error Writer::open(const String &p_path) {
	file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	if (error == OK) {
		error = buffer.resize(1024 * 1024);
		append(reinterpret_cast<const uint8_t *>("WGPUP004"), 8);
	}
	return error;
}

void Writer::flush() {
	if (error == OK && used && !file->store_buffer(buffer.ptr(), used)) {
		error = ERR_FILE_CANT_WRITE;
	}
	used = 0;
}

void Writer::append(const uint8_t *p_bytes, uint64_t p_size) {
	while (p_size && error == OK) {
		if (!used && p_size >= uint64_t(buffer.size())) {
			if (!file->store_buffer(p_bytes, p_size)) {
				error = ERR_FILE_CANT_WRITE;
			}
			return;
		}
		const uint32_t count = uint32_t(MIN(p_size, uint64_t(buffer.size() - used)));
		memcpy(buffer.ptrw() + used, p_bytes, count);
		used += count;
		p_bytes += count;
		p_size -= count;
		if (used == buffer.size()) {
			flush();
		}
	}
}

static void header(uint8_t *&r_destination, uint64_t p_entry, uint64_t p_size, uint32_t p_stride, Kind p_kind) {
	encode_uint64(p_entry, r_destination);
	encode_uint64(p_size, r_destination + 8);
	encode_uint32(p_stride, r_destination + 16);
	encode_uint32(p_stride ? WReadbackCodec::BLOCK_BYTES : 0, r_destination + 20);
	encode_uint32(p_kind, r_destination + 24);
	r_destination += 28;
}

Error prepare(uint64_t p_entry, const Vector<uint8_t> &p_bytes, uint32_t p_stride, const Tiles &p_tiles, Packet &r_packet) {
	if (p_tiles.entries.is_empty()) {
		r_packet.raw_bytes = p_bytes.size();
		r_packet.entries = 1;
		const Error error = r_packet.bytes.resize(p_bytes.size() + 28);
		if (error != OK) {
			return error;
		}
		uint8_t *destination = r_packet.bytes.ptrw();
		header(destination, p_entry, p_bytes.size(), p_stride, BYTES);
		memcpy(destination, p_bytes.ptr(), p_bytes.size());
		return OK;
	}
	const uint32_t columns = (p_tiles.width + TILE_SIZE - 1) / TILE_SIZE;
	const uint64_t pitch = uint64_t(p_tiles.width) * p_tiles.pixel_bytes;
	const uint64_t descriptor_size = 24 + p_tiles.entries.size() * 8;
	r_packet.raw_bytes = descriptor_size;
	r_packet.entries = p_tiles.changed.size() + 1;
	for (uint32_t index : p_tiles.changed) {
		const uint32_t x = (index % columns) * TILE_SIZE;
		const uint32_t y = (index / columns) * TILE_SIZE;
		r_packet.raw_bytes += uint64_t(MIN(TILE_SIZE, p_tiles.width - x)) * MIN(TILE_SIZE, p_tiles.height - y) * p_tiles.pixel_bytes;
	}
	const Error allocation = r_packet.bytes.resize(r_packet.raw_bytes + r_packet.entries * 28);
	if (allocation != OK) {
		return allocation;
	}
	uint8_t *destination = r_packet.bytes.ptrw();
	const uint8_t *bytes = p_bytes.ptr();
	const uint64_t *entries = p_tiles.entries.ptr();
	for (uint32_t index : p_tiles.changed) {
		const uint32_t x = (index % columns) * TILE_SIZE;
		const uint32_t y = (index / columns) * TILE_SIZE;
		const uint32_t row_bytes = MIN(TILE_SIZE, p_tiles.width - x) * p_tiles.pixel_bytes;
		const uint32_t rows = MIN(TILE_SIZE, p_tiles.height - y);
		const uint64_t size = uint64_t(row_bytes) * rows;
		header(destination, entries[index], size, size % 8 == 0 ? p_stride : 0, BYTES);
		const uint8_t *source = bytes + uint64_t(y) * pitch + uint64_t(x) * p_tiles.pixel_bytes;
		for (uint32_t row = 0; row < rows; row++) {
			memcpy(destination, source, row_bytes);
			source += pitch;
			destination += row_bytes;
		}
	}
	header(destination, p_entry, descriptor_size, 0, TILED_IMAGE);
	uint8_t *data = destination;
	encode_uint32(p_tiles.width, data);
	encode_uint32(p_tiles.height, data + 4);
	encode_uint32(p_tiles.pixel_bytes, data + 8);
	encode_uint32(TILE_SIZE, data + 12);
	encode_uint32(TILE_SIZE, data + 16);
	encode_uint32(p_tiles.entries.size(), data + 20);
	for (int64_t i = 0; i < p_tiles.entries.size(); i++) {
		encode_uint64(entries[i], data + 24 + i * 8);
	}
	return OK;
}

Error Writer::store(const Packet &p_packet) {
	raw_bytes += p_packet.raw_bytes;
	entry_count += p_packet.entries;
	append(p_packet.bytes.ptr(), p_packet.bytes.size());
	return error;
}

Error Writer::close() {
	if (file.is_valid()) {
		const uint8_t seal[28] = {};
		append(seal, sizeof(seal));
		flush();
		file->flush();
		if (error == OK) {
			error = file->get_error();
		}
		file.unref();
	}
	buffer.clear();
	return error;
}
}
#endif
