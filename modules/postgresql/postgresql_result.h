// wgodot-changes::file
#pragma once

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"

struct pg_result;

class PostgreSQLResult : public RefCounted {
	GDCLASS(PostgreSQLResult, RefCounted);
	friend class PostgreSQLConnection;

	pg_result *result = nullptr;
	bool valid_cell(int p_row, int p_column) const;
	bool readable_cell(int p_row, int p_column) const;

protected:
	static void _bind_methods();

public:
	enum Status {
		STATUS_UNKNOWN,
		STATUS_COMMAND,
		STATUS_ROWS,
		STATUS_ERROR,
	};
	Status get_status() const;
	String get_error_message() const;
	String get_sqlstate() const;
	int get_row_count() const;
	int get_column_count() const;
	String get_column_name(int p_column) const;
	int64_t get_column_type(int p_column) const;
	int64_t get_affected_rows() const;
	bool is_null(int p_row, int p_column) const;
	bool get_bool(int p_row, int p_column) const;
	int64_t get_int(int p_row, int p_column) const;
	double get_float(int p_row, int p_column) const;
	String get_text(int p_row, int p_column) const;
	PackedByteArray get_bytes(int p_row, int p_column) const;
	~PostgreSQLResult();
};

VARIANT_ENUM_CAST(PostgreSQLResult::Status);
