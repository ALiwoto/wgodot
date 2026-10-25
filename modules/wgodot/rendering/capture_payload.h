// wgodot-changes::file
#pragma once
#ifdef TOOLS_ENABLED
#include "core/io/file_access.h"

namespace WCapturePayload {
enum Kind : uint32_t { BYTES, TILED_IMAGE };
constexpr uint32_t TILE_SIZE = 16;

struct Tiles {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t pixel_bytes = 0;
	Vector<uint64_t> entries;
	Vector<uint32_t> changed;
};

// Preserve unchanged tile IDs; changed slots are assigned by the ordered
// publisher after comparisons have completed on independent workers.
Tiles compare_tiles(const Vector<uint8_t> &p_bytes, uint32_t p_width, uint32_t p_height, uint32_t p_pixel_bytes,
		const Vector<uint8_t> &p_previous_bytes, const Tiles &p_previous);

// Remove exact matches against another image from the pending writes. The
// caller resolves the returned tile indices to that image's immutable IDs.
Vector<uint32_t> reuse_tiles(const Vector<uint8_t> &p_bytes, const Vector<uint8_t> &p_other_bytes, Tiles &r_tiles);

struct Packet {
	Vector<uint8_t> bytes;
	uint64_t raw_bytes = 0;
	uint64_t entries = 0;
};

// Prepare compact, immutable raw records on the CPU planner. The disk queue
// retains changed tiles only, not the full images needed for comparison.
Error prepare(uint64_t p_entry, const Vector<uint8_t> &p_bytes, uint32_t p_stride, const Tiles &p_tiles, Packet &r_packet);

// One ordered writer. Tiles contain raw pixels; only the post-session packer
// compresses them. Image descriptors reference immutable tile entries directly,
// so seeking a frame never requires replaying a chain of preceding frames.
class Writer {
	Ref<FileAccess> file;
	Vector<uint8_t> buffer;
	uint32_t used = 0;
	Error error = OK;
	uint64_t raw_bytes = 0;
	uint64_t entry_count = 0;
	void append(const uint8_t *p_bytes, uint64_t p_size);
	void flush();

public:
	Error open(const String &p_path);
	Error store(const Packet &p_packet);
	Error close();
	uint64_t get_raw_bytes() const { return raw_bytes; }
	uint64_t get_archive_bytes() const { return 8 + raw_bytes + (entry_count + 1) * 28; }
	uint64_t get_entry_count() const { return entry_count; }
};
}
#endif
