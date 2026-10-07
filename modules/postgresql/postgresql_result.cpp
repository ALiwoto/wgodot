// wgodot-changes::file
#include "postgresql_result.h"

#include "core/io/marshalls.h"

#include <libpq-fe.h>

PostgreSQLResult::~PostgreSQLResult() {
	if (result) {
		PQclear(result);
	}
}

PostgreSQLResult::Status PostgreSQLResult::get_status() const {
	if (!result) {
		return STATUS_UNKNOWN;
	}
	switch (PQresultStatus(result)) {
		case PGRES_COMMAND_OK:
			return STATUS_COMMAND;
		case PGRES_TUPLES_OK:
			return STATUS_ROWS;
		default:
			return STATUS_ERROR;
	}
}

String PostgreSQLResult::get_error_message() const {
	return result ? String::utf8(PQresultErrorMessage(result)) : String();
}

String PostgreSQLResult::get_sqlstate() const {
	const char *value = result ? PQresultErrorField(result, PG_DIAG_SQLSTATE) : nullptr;
	return value ? String::utf8(value) : String();
}

int PostgreSQLResult::get_row_count() const {
	return result ? PQntuples(result) : 0;
}

int PostgreSQLResult::get_column_count() const {
	return result ? PQnfields(result) : 0;
}

String PostgreSQLResult::get_column_name(int p_column) const {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), String());
	return String::utf8(PQfname(result, p_column));
}

int64_t PostgreSQLResult::get_column_type(int p_column) const {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), 0);
	return PQftype(result, p_column);
}

int64_t PostgreSQLResult::get_affected_rows() const {
	return result ? String::to_int(PQcmdTuples(result)) : 0;
}

bool PostgreSQLResult::valid_cell(int p_row, int p_column) const {
	return result && p_row >= 0 && p_row < PQntuples(result) && p_column >= 0 && p_column < PQnfields(result);
}

bool PostgreSQLResult::readable_cell(int p_row, int p_column) const {
	return valid_cell(p_row, p_column) && !PQgetisnull(result, p_row, p_column) && PQfformat(result, p_column) == 1;
}

bool PostgreSQLResult::is_null(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!valid_cell(p_row, p_column), true);
	return PQgetisnull(result, p_row, p_column);
}

bool PostgreSQLResult::get_bool(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!readable_cell(p_row, p_column), false);
	ERR_FAIL_COND_V(PQftype(result, p_column) != 16 || PQgetlength(result, p_row, p_column) != 1, false);
	return PQgetvalue(result, p_row, p_column)[0] != 0;
}

int64_t PostgreSQLResult::get_int(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!readable_cell(p_row, p_column), 0);
	const uint8_t *bytes = reinterpret_cast<const uint8_t *>(PQgetvalue(result, p_row, p_column));
	const int length = PQgetlength(result, p_row, p_column);
	switch (PQftype(result, p_column)) {
		case 21:
			ERR_FAIL_COND_V(length != 2, 0);
			return int16_t(BSWAP16(decode_uint16(bytes)));
		case 23:
			ERR_FAIL_COND_V(length != 4, 0);
			return int32_t(BSWAP32(decode_uint32(bytes)));
		case 20:
			ERR_FAIL_COND_V(length != 8, 0);
			return int64_t(BSWAP64(decode_uint64(bytes)));
		default:
			ERR_FAIL_V_MSG(0, "Column is not a PostgreSQL integer. Use an explicit SQL cast or the matching getter.");
	}
}

double PostgreSQLResult::get_float(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!readable_cell(p_row, p_column), 0);
	const uint8_t *bytes = reinterpret_cast<const uint8_t *>(PQgetvalue(result, p_row, p_column));
	const int length = PQgetlength(result, p_row, p_column);
	if (PQftype(result, p_column) == 700) {
		ERR_FAIL_COND_V(length != 4, 0);
		const uint32_t bits = BSWAP32(decode_uint32(bytes));
		float value;
		memcpy(&value, &bits, sizeof(value));
		return value;
	}
	ERR_FAIL_COND_V(PQftype(result, p_column) != 701 || length != 8, 0);
	const uint64_t bits = BSWAP64(decode_uint64(bytes));
	double value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

String PostgreSQLResult::get_text(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!readable_cell(p_row, p_column), String());
	const Oid type = PQftype(result, p_column);
	ERR_FAIL_COND_V_MSG(type != 25 && type != 1042 && type != 1043 && type != 19, String(), "Column is not text. Cast other PostgreSQL types to text explicitly.");
	return String::utf8(PQgetvalue(result, p_row, p_column), PQgetlength(result, p_row, p_column));
}

PackedByteArray PostgreSQLResult::get_bytes(int p_row, int p_column) const {
	ERR_FAIL_COND_V(!readable_cell(p_row, p_column), PackedByteArray());
	ERR_FAIL_COND_V(PQftype(result, p_column) != 17, PackedByteArray());
	PackedByteArray bytes;
	const int length = PQgetlength(result, p_row, p_column);
	bytes.resize(length);
	if (length) {
		memcpy(bytes.ptrw(), PQgetvalue(result, p_row, p_column), length);
	}
	return bytes;
}

void PostgreSQLResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_status"), &PostgreSQLResult::get_status);
	ClassDB::bind_method(D_METHOD("get_error_message"), &PostgreSQLResult::get_error_message);
	ClassDB::bind_method(D_METHOD("get_sqlstate"), &PostgreSQLResult::get_sqlstate);
	ClassDB::bind_method(D_METHOD("get_row_count"), &PostgreSQLResult::get_row_count);
	ClassDB::bind_method(D_METHOD("get_column_count"), &PostgreSQLResult::get_column_count);
	ClassDB::bind_method(D_METHOD("get_column_name", "column"), &PostgreSQLResult::get_column_name);
	ClassDB::bind_method(D_METHOD("get_column_type", "column"), &PostgreSQLResult::get_column_type);
	ClassDB::bind_method(D_METHOD("get_affected_rows"), &PostgreSQLResult::get_affected_rows);
	ClassDB::bind_method(D_METHOD("is_null", "row", "column"), &PostgreSQLResult::is_null);
	ClassDB::bind_method(D_METHOD("get_bool", "row", "column"), &PostgreSQLResult::get_bool);
	ClassDB::bind_method(D_METHOD("get_int", "row", "column"), &PostgreSQLResult::get_int);
	ClassDB::bind_method(D_METHOD("get_float", "row", "column"), &PostgreSQLResult::get_float);
	ClassDB::bind_method(D_METHOD("get_text", "row", "column"), &PostgreSQLResult::get_text);
	ClassDB::bind_method(D_METHOD("get_bytes", "row", "column"), &PostgreSQLResult::get_bytes);
	BIND_ENUM_CONSTANT(STATUS_UNKNOWN);
	BIND_ENUM_CONSTANT(STATUS_COMMAND);
	BIND_ENUM_CONSTANT(STATUS_ROWS);
	BIND_ENUM_CONSTANT(STATUS_ERROR);
}
