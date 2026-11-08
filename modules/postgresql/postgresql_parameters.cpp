// wgodot-changes::file
#include "postgresql_parameters.h"

#include "core/io/marshalls.h"

namespace {
template <typename T>
PackedByteArray encode_array(const Vector<T> &p_values, uint32_t p_element_oid) {
	// PostgreSQL's binary array header followed by length-prefixed, network-order
	// elements. Packed arrays contain no nulls and have one dimension (or zero
	// dimensions for an empty array). No text formatting or SQL interpolation.
	constexpr int stride = 4 + sizeof(T);
	ERR_FAIL_COND_V(p_values.size() > (INT32_MAX - 20) / stride, PackedByteArray());
	PackedByteArray bytes;
	bytes.resize(p_values.is_empty() ? 12 : 20 + p_values.size() * stride);
	uint8_t *out = bytes.ptrw();
	encode_uint32(BSWAP32(p_values.is_empty() ? 0 : 1), out);
	encode_uint32(0, out + 4);
	encode_uint32(BSWAP32(p_element_oid), out + 8);
	if (p_values.is_empty()) {
		return bytes;
	}
	encode_uint32(BSWAP32(uint32_t(p_values.size())), out + 12);
	encode_uint32(BSWAP32(1), out + 16);
	for (int64_t i = 0; i < p_values.size(); i++) {
		uint8_t *element = out + 20 + i * stride;
		encode_uint32(BSWAP32(uint32_t(sizeof(T))), element);
		if constexpr (sizeof(T) == 4) {
			uint32_t bits;
			memcpy(&bits, &p_values[i], sizeof(bits));
			encode_uint32(BSWAP32(bits), element + 4);
		} else {
			uint64_t bits;
			memcpy(&bits, &p_values[i], sizeof(bits));
			encode_uint64(BSWAP64(bits), element + 4);
		}
	}
	return bytes;
}
} // namespace

void PostgreSQLParameters::append_int32_array(const PackedInt32Array &p_values) {
	parameters.push_back({ 1007, encode_array(p_values, 23), false });
}

void PostgreSQLParameters::append_int64_array(const PackedInt64Array &p_values) {
	parameters.push_back({ 1016, encode_array(p_values, 20), false });
}

void PostgreSQLParameters::append_float64_array(const PackedFloat64Array &p_values) {
	parameters.push_back({ 1022, encode_array(p_values, 701), false });
}

void PostgreSQLParameters::append_null() {
	parameters.push_back({ 0, PackedByteArray(), true });
}

void PostgreSQLParameters::append_bool(bool p_value) {
	PackedByteArray bytes;
	bytes.push_back(p_value ? 1 : 0);
	parameters.push_back({ 16, bytes, false });
}

void PostgreSQLParameters::append_int(int64_t p_value) {
	PackedByteArray bytes;
	bytes.resize(8);
	encode_uint64(BSWAP64(uint64_t(p_value)), bytes.ptrw());
	parameters.push_back({ 20, bytes, false });
}

void PostgreSQLParameters::append_float(double p_value) {
	uint64_t bits;
	memcpy(&bits, &p_value, sizeof(bits));
	PackedByteArray bytes;
	bytes.resize(8);
	encode_uint64(BSWAP64(bits), bytes.ptrw());
	parameters.push_back({ 701, bytes, false });
}

void PostgreSQLParameters::append_text(const String &p_value) {
	parameters.push_back({ 25, p_value.to_utf8_buffer(), false });
}

void PostgreSQLParameters::append_bytes(const PackedByteArray &p_value) {
	parameters.push_back({ 17, p_value, false });
}

void PostgreSQLParameters::_bind_methods() {
	ClassDB::bind_method(D_METHOD("append_null"), &PostgreSQLParameters::append_null);
	ClassDB::bind_method(D_METHOD("append_bool", "value"), &PostgreSQLParameters::append_bool);
	ClassDB::bind_method(D_METHOD("append_int", "value"), &PostgreSQLParameters::append_int);
	ClassDB::bind_method(D_METHOD("append_float", "value"), &PostgreSQLParameters::append_float);
	ClassDB::bind_method(D_METHOD("append_text", "value"), &PostgreSQLParameters::append_text);
	ClassDB::bind_method(D_METHOD("append_bytes", "value"), &PostgreSQLParameters::append_bytes);
	ClassDB::bind_method(D_METHOD("append_int32_array", "values"), &PostgreSQLParameters::append_int32_array);
	ClassDB::bind_method(D_METHOD("append_int64_array", "values"), &PostgreSQLParameters::append_int64_array);
	ClassDB::bind_method(D_METHOD("append_float64_array", "values"), &PostgreSQLParameters::append_float64_array);
	ClassDB::bind_method(D_METHOD("size"), &PostgreSQLParameters::size);
	ClassDB::bind_method(D_METHOD("clear"), &PostgreSQLParameters::clear);
}
