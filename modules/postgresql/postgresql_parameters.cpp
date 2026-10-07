// wgodot-changes::file
#include "postgresql_parameters.h"

#include "core/io/marshalls.h"

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
	ClassDB::bind_method(D_METHOD("size"), &PostgreSQLParameters::size);
	ClassDB::bind_method(D_METHOD("clear"), &PostgreSQLParameters::clear);
}
