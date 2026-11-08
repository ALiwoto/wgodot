// wgodot-changes::file
#pragma once

#include "core/io/marshalls.h"
#include "core/variant/variant.h"

namespace WGodotSerialization {
enum class Kind {
	BOOL,
	INT8,
	UINT8,
	INT16,
	UINT16,
	INT32,
	UINT32,
	INT64,
	UINT64,
	SINT32,
	SINT64,
	FIXED32,
	SFIXED32,
	FIXED64,
	SFIXED64,
	FLOAT,
	DOUBLE,
	STRING,
	BYTES,
	MESSAGE,
	VECTOR3,
	INVALID,
};

Kind kind_from_name(const String &p_name);
const char *kind_name(Kind p_kind);
int wire_type(Kind p_kind);
int binary_width(Kind p_kind);

struct Writer {
	PackedByteArray data;
	bool valid = true;
	bool big_endian = false;
	int depth = 0;

	void byte(uint8_t p_value) { data.push_back(p_value); }
	void varint(uint64_t p_value) {
		while (p_value >= 128) {
			byte(uint8_t(p_value) | 0x80);
			p_value >>= 7;
		}
		byte(uint8_t(p_value));
	}
	void fixed(uint64_t p_value, int p_width) {
		for (int i = 0; i < p_width; ++i) {
			byte(uint8_t(p_value >> ((big_endian ? p_width - 1 - i : i) * 8)));
		}
	}
	void raw(const uint8_t *p_bytes, int64_t p_size) {
		const int64_t offset = data.size();
		if (p_size < 0 || p_size > INT64_MAX - offset || data.resize(offset + p_size) != OK) {
			valid = false;
			return;
		}
		if (p_size) {
			memcpy(data.ptrw() + offset, p_bytes, p_size);
		}
	}
	void tag(uint32_t p_id, int p_wire) { varint((uint64_t(p_id) << 3) | p_wire); }
};

struct Reader {
	const uint8_t *data = nullptr;
	int64_t size = 0;
	int64_t position = 0;
	bool valid = true;
	bool big_endian = false;
	int depth = 0;

	int64_t remaining() const { return size - position; }
	bool take(int64_t p_size, const uint8_t *&r_data) {
		if (!valid || p_size < 0 || p_size > remaining()) {
			valid = false;
			return false;
		}
		r_data = data ? data + position : nullptr;
		position += p_size;
		return true;
	}
	uint64_t varint() {
		uint64_t value = 0;
		for (int shift = 0; shift < 70; shift += 7) {
			const uint8_t *item;
			if (!take(1, item) || (shift == 63 && *item > 1)) {
				valid = false;
				return 0;
			}
			value |= uint64_t(*item & 0x7f) << shift;
			if (!(*item & 0x80)) {
				return value;
			}
		}
		valid = false;
		return 0;
	}
	uint64_t fixed(int p_width) {
		const uint8_t *bytes;
		if (!take(p_width, bytes)) {
			return 0;
		}
		uint64_t value = 0;
		for (int i = 0; i < p_width; ++i) {
			value |= uint64_t(bytes[i]) << ((big_endian ? p_width - 1 - i : i) * 8);
		}
		return value;
	}
	Reader slice(uint64_t p_size) {
		Reader result;
		result.depth = depth + 1;
		result.big_endian = big_endian;
		if (p_size > uint64_t(remaining()) || result.depth > 100 || !take(int64_t(p_size), result.data)) {
			valid = result.valid = false;
			return result;
		}
		result.size = int64_t(p_size);
		return result;
	}
	bool skip(int p_wire, uint32_t p_id);
};

inline uint64_t zigzag(int64_t p_value) {
	return (uint64_t(p_value) << 1) ^ uint64_t(p_value >> 63);
}
inline int64_t unzigzag(uint64_t p_value) {
	return int64_t((p_value >> 1) ^ (uint64_t(0) - (p_value & 1)));
}

void write_number(Writer &r_writer, Kind p_kind, int64_t p_integer, double p_float, bool p_protobuf);
bool read_number(Reader &r_reader, Kind p_kind, int64_t &r_integer, double &r_float, bool p_protobuf);
void write_bytes(Writer &r_writer, const uint8_t *p_data, int64_t p_size, bool p_protobuf);
bool read_bytes(Reader &r_reader, PackedByteArray &r_bytes, bool p_protobuf);
bool read_string(Reader &r_reader, String &r_string, bool p_protobuf);
String encode_base64(const uint8_t *p_data, int64_t p_size);
bool decode_base64(const String &p_text, PackedByteArray &r_bytes);
bool json_integer(const Variant &p_value, int64_t &r_integer);
} // namespace WGodotSerialization
