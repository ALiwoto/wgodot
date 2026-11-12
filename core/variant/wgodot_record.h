// wgodot-changes::file
#pragma once

#include "core/math/wgodot_scalar.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

// Record dependencies use declared names. A recursive field such as Array[Tree]
// must not keep its own schema alive through a cycle of type descriptors.
struct WRecordFieldType {
	Variant::Type builtin = Variant::NIL;
	StringName class_name;
	StringName record_name;
	ObjectID declaration;
	WGodotScalar::Type scalar = WGodotScalar::NONE;
	bool value_container = false;
	Vector<WRecordFieldType> elements;

	bool convert(Variant &r_value, bool p_literal = false) const;
	void convert_storage(Variant &r_value) const;
	Variant instantiate(const Variant &p_literal, bool &r_valid) const;
};

// Immutable once published. Default literals contain plain data, not typed
// containers or record instances, so recursive declarations cannot own cycles.
class WRecordSchema : public RefCounted {
	GDCLASS(WRecordSchema, RefCounted);

public:
	struct Field {
		StringName name;
		WRecordFieldType type;
	};

private:
	StringName name;
	String source;
	Vector<Field> fields;
	Vector<Variant> defaults;
	HashMap<StringName, int> indices;
	bool sealed = false;

protected:
	static void _bind_methods() {}

public:
	void initialize(const StringName &p_name, const String &p_source, const Vector<Field> &p_fields, const Vector<Variant> &p_defaults);
	const StringName &get_name() const { return name; }
	const String &get_source() const { return source; }
	int field_count() const { return fields.size(); }
	int find_field(const StringName &p_name) const;
	const Field &field(int p_index) const { return fields[p_index]; }
	const Variant &default_literal(int p_index) const { return defaults[p_index]; }
	bool is_sealed() const { return sealed; }
};

struct WRecordData;

class WRecord {
	WRecordData *data = nullptr;
	void retain() const;
	void release();
	void make_unique();

public:
	Ref<WRecordSchema> schema() const;
	bool is_empty() const { return data == nullptr; }
	const Variant &get(int p_index) const;
	Variant &write(int p_index);
	bool set(int p_index, const Variant &p_value);
	void set_validated(int p_index, const Variant &p_value);
	Variant get_named(const StringName &p_name, bool &r_valid) const;
	void set_named(const StringName &p_name, const Variant &p_value, bool &r_valid);
	uint32_t recursive_hash(int p_depth) const;
	bool recursive_equal(const WRecord &p_other, int p_depth) const;
	bool operator==(const WRecord &p_other) const { return recursive_equal(p_other, 0); }
	bool operator!=(const WRecord &p_other) const { return !(*this == p_other); }
	operator String() const;
	const void *id() const { return data; }

	WRecord() = default;
	WRecord(const Ref<WRecordSchema> &p_schema, const Vector<Variant> &p_fields);
	static WRecord create(const Ref<WRecordSchema> &p_schema, const Dictionary &p_fields, bool &r_valid);
	WRecord(const WRecord &p_other);
	WRecord(WRecord &&p_other) noexcept;
	WRecord &operator=(const WRecord &p_other);
	WRecord &operator=(WRecord &&p_other) noexcept;
	~WRecord();
};

namespace WRecordSchemas {
// One current schema per declared name. Recompilation replaces that entry;
// existing values retain their immutable layout until they are released.
void publish(const Ref<WRecordSchema> &p_schema);
Ref<WRecordSchema> find(const StringName &p_name);
Ref<WRecordSchema> resolve(const StringName &p_name);
WRecord default_value(const StringName &p_name, bool &r_valid);
void clear();
} // namespace WRecordSchemas
