// wgodot-changes::file
#include "wire.h"

#include "core/crypto/crypto_core.h"

#include <charconv>

namespace WGodotSerialization {
namespace {
constexpr const char *names[] = {
	"bool",
	"int8",
	"uint8",
	"int16",
	"uint16",
	"int32",
	"uint32",
	"int64",
	"uint64",
	"sint32",
	"sint64",
	"fixed32",
	"sfixed32",
	"fixed64",
	"sfixed64",
	"float",
	"double",
	"string",
	"bytes",
	"message",
	"Vector3",
};
}

Kind kind_from_name(const String &p_name) {
	if (p_name == "vector3") {
		return Kind::VECTOR3;
	}
	if (p_name == "int") {
		return Kind::INT32;
	}
	if (p_name == "uint") {
		return Kind::UINT32;
	}
	if (p_name == "datetime") {
		return Kind::INT64;
	}
	if (p_name == "float32") {
		return Kind::FLOAT;
	}
	if (p_name == "float64") {
		return Kind::DOUBLE;
	}
	for (int i = 0; i < int(Kind::INVALID); ++i) {
		if (p_name == names[i]) {
			return Kind(i);
		}
	}
	return Kind::INVALID;
}

const char *kind_name(Kind p_kind) {
	static constexpr const char *cpp_names[] = {
		"BOOL",
		"INT8",
		"UINT8",
		"INT16",
		"UINT16",
		"INT32",
		"UINT32",
		"INT64",
		"UINT64",
		"SINT32",
		"SINT64",
		"FIXED32",
		"SFIXED32",
		"FIXED64",
		"SFIXED64",
		"FLOAT",
		"DOUBLE",
		"STRING",
		"BYTES",
		"MESSAGE",
		"VECTOR3",
		"INVALID",
	};
	return cpp_names[int(p_kind)];
}

int wire_type(Kind p_kind) {
	switch (p_kind) {
		case Kind::FIXED64:
		case Kind::SFIXED64:
		case Kind::DOUBLE:
			return 1;
		case Kind::STRING:
		case Kind::BYTES:
		case Kind::MESSAGE:
			return 2;
		case Kind::FIXED32:
		case Kind::SFIXED32:
		case Kind::FLOAT:
			return 5;
		case Kind::INVALID:
		case Kind::VECTOR3:
			return -1;
		default:
			return 0;
	}
}

int binary_width(Kind p_kind) {
	switch (p_kind) {
		case Kind::BOOL:
		case Kind::INT8:
		case Kind::UINT8:
			return 1;
		case Kind::INT16:
		case Kind::UINT16:
			return 2;
		case Kind::INT32:
		case Kind::UINT32:
		case Kind::FLOAT:
			return 4;
		case Kind::INT64:
		case Kind::UINT64:
		case Kind::DOUBLE:
			return 8;
		default:
			return 0;
	}
}

bool Reader::skip(int p_wire, uint32_t p_id) {
	switch (p_wire) {
		case 0:
			varint();
			break;
		case 1:
			fixed(8);
			break;
		case 2: {
			const uint64_t length = varint();
			const uint8_t *bytes;
			if (length > uint64_t(remaining()) || !take(int64_t(length), bytes)) {
				valid = false;
			}
		} break;
		case 3: {
			if (++depth > 100) {
				valid = false;
				break;
			}
			while (valid && remaining()) {
				const uint64_t tag = varint();
				if (!(tag >> 3) || (tag >> 3) > 536870911) {
					valid = false;
					break;
				}
				if ((tag & 7) == 4) {
					valid = uint32_t(tag >> 3) == p_id;
					--depth;
					return valid;
				}
				skip(tag & 7, tag >> 3);
			}
			valid = false;
			--depth;
		} break;
		case 5:
			fixed(4);
			break;
		default:
			valid = false;
			break;
	}
	return valid;
}

void write_number(Writer &r_writer, Kind p_kind, int64_t p_integer, double p_float, bool p_protobuf) {
	if (p_kind == Kind::FLOAT) {
		float number = float(p_float);
		uint32_t bits;
		memcpy(&bits, &number, 4);
		r_writer.fixed(bits, 4);
	} else if (p_kind == Kind::DOUBLE) {
		uint64_t bits;
		memcpy(&bits, &p_float, 8);
		r_writer.fixed(bits, 8);
	} else if (!p_protobuf) {
		const int width = binary_width(p_kind);
		if (!width) {
			r_writer.valid = false;
			return;
		}
		r_writer.fixed(uint64_t(p_integer), width);
	} else if (p_kind == Kind::SINT32 || p_kind == Kind::SINT64) {
		r_writer.varint(zigzag(p_kind == Kind::SINT32 ? int32_t(p_integer) : p_integer));
	} else if (wire_type(p_kind) == 1 || wire_type(p_kind) == 5) {
		r_writer.fixed(uint64_t(p_integer), wire_type(p_kind) == 1 ? 8 : 4);
	} else {
		if (p_kind == Kind::INT32) {
			p_integer = int32_t(p_integer);
		}
		if (p_kind == Kind::UINT32) {
			p_integer = uint32_t(p_integer);
		}
		r_writer.varint(uint64_t(p_integer));
	}
}

bool read_number(Reader &r_reader, Kind p_kind, int64_t &r_integer, double &r_float, bool p_protobuf) {
	uint64_t bits;
	if (p_kind == Kind::FLOAT || p_kind == Kind::DOUBLE) {
		bits = r_reader.fixed(p_kind == Kind::FLOAT ? 4 : 8);
		if (p_kind == Kind::FLOAT) {
			const uint32_t small = uint32_t(bits);
			float number;
			memcpy(&number, &small, 4);
			r_float = number;
		} else {
			memcpy(&r_float, &bits, 8);
		}
		return r_reader.valid;
	}
	if (!p_protobuf) {
		const int width = binary_width(p_kind);
		if (!width) {
			r_reader.valid = false;
			return false;
		}
		bits = r_reader.fixed(width);
	} else if (wire_type(p_kind) == 1 || wire_type(p_kind) == 5) {
		bits = r_reader.fixed(wire_type(p_kind) == 1 ? 8 : 4);
	} else {
		bits = r_reader.varint();
	}
	switch (p_kind) {
		case Kind::BOOL:
			r_integer = bits != 0;
			break;
		case Kind::INT8:
			r_integer = int8_t(bits);
			break;
		case Kind::UINT8:
			r_integer = uint8_t(bits);
			break;
		case Kind::INT16:
			r_integer = int16_t(bits);
			break;
		case Kind::UINT16:
			r_integer = uint16_t(bits);
			break;
		case Kind::INT32:
		case Kind::SFIXED32:
			r_integer = int32_t(bits);
			break;
		case Kind::UINT32:
		case Kind::FIXED32:
			r_integer = uint32_t(bits);
			break;
		case Kind::SINT32:
			r_integer = int32_t(unzigzag(bits));
			break;
		case Kind::SINT64:
			r_integer = unzigzag(bits);
			break;
		default:
			r_integer = int64_t(bits);
			break;
	}
	return r_reader.valid;
}

void write_bytes(Writer &r_writer, const uint8_t *p_data, int64_t p_size, bool p_protobuf) {
	if (p_protobuf) {
		r_writer.varint(p_size);
	} else {
		r_writer.fixed(p_size, 4);
	}
	r_writer.raw(p_data, p_size);
}

bool read_bytes(Reader &r_reader, PackedByteArray &r_bytes, bool p_protobuf) {
	const uint64_t size = p_protobuf ? r_reader.varint() : r_reader.fixed(4);
	const uint8_t *bytes;
	if (size > uint64_t(r_reader.remaining()) || !r_reader.take(int64_t(size), bytes)) {
		r_reader.valid = false;
		return false;
	}
	if (r_bytes.resize(int64_t(size)) != OK) {
		return false;
	}
	if (size) {
		memcpy(r_bytes.ptrw(), bytes, size);
	}
	return true;
}

bool read_string(Reader &r_reader, String &r_string, bool p_protobuf) {
	const uint64_t size = p_protobuf ? r_reader.varint() : r_reader.fixed(4);
	const uint8_t *bytes;
	if (size > uint64_t(r_reader.remaining()) || !r_reader.take(int64_t(size), bytes)) {
		r_reader.valid = false;
		return false;
	}
	r_string.clear();
	return r_string.append_utf8(reinterpret_cast<const char *>(bytes), int64_t(size)) == OK;
}

String encode_base64(const uint8_t *p_data, int64_t p_size) {
	return p_size ? CryptoCore::b64_encode_str(p_data, p_size) : String();
}

bool json_integer(const Variant &p_value, int64_t &r_integer) {
	if (p_value.get_type() == Variant::INT) {
		r_integer = p_value;
		return true;
	}
	if (p_value.get_type() == Variant::STRING) {
		const CharString text = String(p_value).utf8();
		const auto parsed = std::from_chars(text.get_data(), text.get_data() + text.length(), r_integer);
		return parsed.ec == std::errc() && parsed.ptr == text.get_data() + text.length();
	}
	if (p_value.get_type() == Variant::FLOAT) {
		const double number = p_value;
		if (!(number >= -9223372036854775808.0 && number < 9223372036854775808.0)) {
			return false;
		}
		r_integer = int64_t(number);
		return double(r_integer) == number;
	}
	return false;
}

bool decode_base64(const String &p_text, PackedByteArray &r_bytes) {
	const CharString input = p_text.utf8();
	if (r_bytes.resize(input.length() / 4 * 3 + 3) != OK) {
		return false;
	}
	size_t length = 0;
	if (CryptoCore::b64_decode(r_bytes.ptrw(), r_bytes.size(), &length, reinterpret_cast<const uint8_t *>(input.get_data()), input.length()) != OK) {
		return false;
	}
	return r_bytes.resize(length) == OK;
}
} // namespace WGodotSerialization
