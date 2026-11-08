// wgodot-changes::file
#pragma once

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"

class PostgreSQLParameters : public RefCounted {
	GDCLASS(PostgreSQLParameters, RefCounted);
	friend class PostgreSQLConnection;

	struct Parameter {
		uint32_t oid = 0;
		PackedByteArray bytes;
		bool null = false;
	};
	Vector<Parameter> parameters;

protected:
	static void _bind_methods();

public:
	void append_null();
	void append_bool(bool p_value);
	void append_int(int64_t p_value);
	void append_float(double p_value);
	void append_text(const String &p_value);
	void append_bytes(const PackedByteArray &p_value);
	void append_int32_array(const PackedInt32Array &p_values);
	void append_int64_array(const PackedInt64Array &p_values);
	void append_float64_array(const PackedFloat64Array &p_values);
	int size() const { return parameters.size(); }
	void clear() { parameters.clear(); }
};
