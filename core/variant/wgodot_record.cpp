// wgodot-changes::file
#include "wgodot_record.h"

#include "core/object/script_language.h"
#include "core/io/wgodot_named_types.h"
#include "core/object/wgodot_native_interfaces.h"
#include "core/object/wgodot_class_references.h"
#include "core/os/rw_lock.h"
#include "core/variant/variant_internal.h"
#include "core/variant/container_type_validate.h"

struct WRecordData {
	SafeRefCount references;
	Ref<WRecordSchema> schema;
	Vector<Variant> fields;

	WRecordData(const Ref<WRecordSchema> &p_schema, const Vector<Variant> &p_fields) :
			schema(p_schema), fields(p_fields) {
		references.init();
	}
};

namespace {
struct SchemaEntry {
	Ref<WRecordSchema> schema;
	WRecord defaults;
};

RWLock schema_lock;
HashMap<StringName, SchemaEntry> schemas;
uint64_t schema_epoch = 0;
thread_local int construction_depth = 0;
thread_local HashSet<StringName> constructing_defaults;

// Copy only the declaration data. It cannot retain typed schemas or object
// graphs, and callers cannot mutate the defaults through an old container alias.
bool literal_data(const Variant &p_value, Variant &r_result, int p_depth = 0) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return false;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array result;
		for (const Variant &item : source) {
			Variant value;
			if (!literal_data(item, value, p_depth + 1)) {
				return false;
			}
			result.push_back(value);
		}
		result.make_read_only();
		r_result = result;
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary result;
		for (const KeyValue<Variant, Variant> &item : source) {
			Variant key;
			Variant value;
			if (!literal_data(item.key, key, p_depth + 1) || !literal_data(item.value, value, p_depth + 1)) {
				return false;
			}
			result[key] = value;
		}
		result.make_read_only();
		r_result = result;
	} else if (p_value.get_type() == Variant::WRECORD) {
		const WRecord source = p_value;
		Dictionary result;
		if (source.is_empty()) {
			return false;
		}
		for (int index = 0; index < source.schema()->field_count(); ++index) {
			Variant value;
			if (!literal_data(source.get(index), value, p_depth + 1)) {
				return false;
			}
			result[source.schema()->field(index).name] = value;
		}
		result.make_read_only();
		r_result = result;
	} else if (p_value.get_type() == Variant::OBJECT) {
		const StringName name = WGodotClassReferences::name(p_value);
		if (name != StringName()) {
			r_result = name; // Keep schema defaults free of owning script references.
			return true;
		}
		return p_value.operator ObjectID() == ObjectID();
	} else if (p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL) {
		return false;
	} else {
		r_result = p_value;
		if (Variant::is_packed_array_type(p_value.get_type())) {
			r_result.wgodot_make_value_container();
		}
	}
	return true;
}

Variant type_declaration(const WRecordFieldType &p_type) {
	if (p_type.builtin == Variant::WRECORD) {
		return Variant();
	}
	return p_type.declaration.is_valid() ? Variant(ObjectDB::get_instance(p_type.declaration)) : Variant();
}

StringName type_name(const WRecordFieldType &p_type) {
	return p_type.builtin == Variant::WRECORD ? p_type.record_name : p_type.class_name;
}

bool same_element_type(const ContainerType &p_container, const WRecordFieldType &p_type) {
	if (p_type.value_container || p_type.scalar != WGodotScalar::NONE || !p_type.elements.is_empty()) {
		return false;
	}
	return p_container.builtin_type == p_type.builtin && p_container.class_name == type_name(p_type) &&
			(p_container.script.is_valid() ? p_container.script->get_instance_id() : ObjectID()) == p_type.declaration;
}

uint32_t field_hash(const Variant &p_value, int p_depth) {
	ERR_FAIL_COND_V(p_depth > Variant::MAX_RECURSION_DEPTH, 0);
	if (p_value.get_type() == Variant::DICTIONARY) {
		uint32_t hash = hash_murmur3_one_32(Variant::DICTIONARY);
		const Dictionary values = p_value;
		for (const auto &entry : values) {
			hash += hash_murmur3_one_32(field_hash(entry.value, p_depth + 1), field_hash(entry.key, p_depth + 1));
		}
		return hash_fmix32(hash);
	}
	if (p_value.get_type() == Variant::ARRAY) {
		uint32_t hash = hash_murmur3_one_32(Variant::ARRAY);
		const Array values = p_value;
		for (const Variant &value : values) {
			hash = hash_murmur3_one_32(field_hash(value, p_depth + 1), hash);
		}
		return hash_fmix32(hash);
	}
	return p_value.recursive_hash(p_depth);
}
} // namespace

void WRecordSchema::initialize(const StringName &p_name, const String &p_source, const Vector<Field> &p_fields, const Vector<Variant> &p_defaults) {
	ERR_FAIL_COND(sealed || p_name == StringName() || p_fields.size() != p_defaults.size());
	HashMap<StringName, int> field_indices;
	for (int index = 0; index < p_fields.size(); ++index) {
		ERR_FAIL_COND_MSG(field_indices.has(p_fields[index].name), "Duplicate record field.");
		field_indices.insert(p_fields[index].name, index);
	}
	name = p_name;
	source = p_source;
	fields = p_fields;
	defaults.resize(p_defaults.size());
	for (int index = 0; index < p_defaults.size(); ++index) {
		ERR_FAIL_COND_MSG(!literal_data(p_defaults[index], defaults.write[index]), "Record defaults must contain finite-depth data literals without live objects.");
	}
	indices = std::move(field_indices);
	sealed = true;
}

int WRecordSchema::find_field(const StringName &p_name) const {
	const int *index = indices.getptr(p_name);
	return index ? *index : -1;
}

void WRecordFieldType::convert_storage(Variant &r_value) const {
	switch (scalar) {
		case WGodotScalar::FLOAT32:
			r_value = WGodotScalar::convert<float>(r_value);
			break;
		case WGodotScalar::INT32:
			r_value = WGodotScalar::convert<int32_t>(r_value);
			break;
		case WGodotScalar::UINT8:
			r_value = WGodotScalar::convert<uint8_t>(r_value);
			break;
		default:
			break;
	}
	if (value_container) {
		r_value.wgodot_make_value_container();
	}
}

bool WRecordFieldType::convert(Variant &r_value, bool p_literal) const {
	if (builtin == Variant::NIL) {
		return true;
	}
	if (builtin == Variant::WRECORD) {
		if (p_literal && r_value.get_type() == Variant::NIL) {
			bool valid = false;
			r_value = WRecordSchemas::default_value(record_name, valid);
			return valid;
		}
		if (p_literal && r_value.get_type() == Variant::DICTIONARY) {
			bool valid = false;
			r_value = WRecord::create(WRecordSchemas::resolve(record_name), r_value, valid);
			return valid;
		}
		if (r_value.get_type() != Variant::WRECORD) {
			return false;
		}
		const WRecord record = r_value;
		return !record.is_empty() && record.schema()->get_name() == record_name;
	}
	if (builtin == Variant::OBJECT) {
		const StringName base = WGodotClassReferences::constraint_base(class_name);
		if (base != StringName()) {
			if (p_literal && r_value.get_type() == Variant::STRING_NAME) {
				r_value = WGodotClassReferences::resolve(r_value);
				if (r_value.is_null()) {
					return false;
				}
			}
			return WGodotClassReferences::accepts(r_value, base);
		}
		if (r_value.get_type() == Variant::NIL) {
			return true;
		}
		if (r_value.get_type() != Variant::OBJECT) {
			return false;
		}
		Object *object = r_value.get_validated_object();
		if (!object) {
			return r_value.operator ObjectID() == ObjectID();
		}
		if (class_name != StringName() && !WGodotNativeInterfaces::is_instance(object, class_name)) {
			return false;
		}
		if (declaration.is_valid()) {
			Script *expected = Object::cast_to<Script>(ObjectDB::get_instance(declaration));
			ScriptInstance *instance = object->get_script_instance();
			return expected && instance && expected->wgodot_is_type_compatible(instance->get_script());
		}
		return true;
	}
	if (r_value.get_type() != builtin) {
		if (!Variant::can_convert_strict(r_value.get_type(), builtin)) {
			return false;
		}
		Variant converted;
		const Variant *argument = &r_value;
		Callable::CallError error;
		Variant::construct(builtin, converted, &argument, 1, error);
		if (error.error != Callable::CallError::CALL_OK) {
			return false;
		}
		r_value = converted;
	}
	if (builtin == Variant::ARRAY && elements.size() == 1) {
		const Array input = r_value;
		const WRecordFieldType &element = elements[0];
		if (!p_literal && same_element_type(input.get_element_type(), element)) {
			convert_storage(r_value);
			return true;
		}
		Array output;
		output.set_typed(element.builtin, type_name(element), type_declaration(element));
		for (const Variant &item : input) {
			Variant converted = item;
			if (!element.convert(converted, p_literal)) {
				return false;
			}
			output.push_back(converted);
		}
		r_value = output;
	} else if (builtin == Variant::DICTIONARY && elements.size() == 2) {
		const Dictionary input = r_value;
		const WRecordFieldType &key_type = elements[0];
		const WRecordFieldType &value_type = elements[1];
		if (!p_literal && same_element_type(input.get_key_type(), key_type) && same_element_type(input.get_value_type(), value_type)) {
			convert_storage(r_value);
			return true;
		}
		Dictionary output;
		output.set_typed(key_type.builtin, type_name(key_type), type_declaration(key_type),
				value_type.builtin, type_name(value_type), type_declaration(value_type));
		for (const KeyValue<Variant, Variant> &item : input) {
			Variant key = item.key;
			Variant value = item.value;
			if (!key_type.convert(key, p_literal) || !value_type.convert(value, p_literal)) {
				return false;
			}
			output[key] = value;
		}
		r_value = output;
	}
	convert_storage(r_value);
	return true;
}

Variant WRecordFieldType::instantiate(const Variant &p_literal, bool &r_valid) const {
	Variant result = p_literal;
	r_valid = convert(result, true);
	return result;
}

Ref<WRecordSchema> WRecord::schema() const {
	return data ? data->schema : Ref<WRecordSchema>();
}

void WRecord::retain() const {
	if (data) {
		data->references.ref();
	}
}

void WRecord::release() {
	if (data && data->references.unref()) {
		memdelete(data);
	}
	data = nullptr;
}

void WRecord::make_unique() {
	if (data->references.get() > 1) {
		WRecordData *copy = memnew(WRecordData(data->schema, data->fields));
		release();
		data = copy;
	}
}

const Variant &WRecord::get(int p_index) const {
	DEV_ASSERT(data && p_index >= 0 && p_index < data->fields.size());
	return data->fields[p_index];
}

Variant &WRecord::write(int p_index) {
	DEV_ASSERT(data && p_index >= 0 && p_index < data->fields.size());
	make_unique();
	return data->fields.write[p_index];
}

bool WRecord::set(int p_index, const Variant &p_value) {
	if (!data || p_index < 0 || p_index >= data->fields.size()) {
		return false;
	}
	Variant value = p_value;
	if (!data->schema->field(p_index).type.convert(value)) {
		return false;
	}
	write(p_index) = value;
	return true;
}

void WRecord::set_validated(int p_index, const Variant &p_value) {
	Variant value = p_value;
	data->schema->field(p_index).type.convert_storage(value);
	write(p_index) = value;
}

Variant WRecord::get_named(const StringName &p_name, bool &r_valid) const {
	const int index = data ? data->schema->find_field(p_name) : -1;
	r_valid = index >= 0;
	return r_valid ? get(index) : Variant();
}

void WRecord::set_named(const StringName &p_name, const Variant &p_value, bool &r_valid) {
	const int index = data ? data->schema->find_field(p_name) : -1;
	r_valid = set(index, p_value);
}

uint32_t WRecord::recursive_hash(int p_depth) const {
	if (!data || p_depth > Variant::MAX_RECURSION_DEPTH) {
		return 0;
	}
	uint32_t hash = hash_murmur3_one_32(data->schema->get_name().hash());
	for (const Variant &value : data->fields) {
		hash = hash_murmur3_one_32(field_hash(value, p_depth + 1), hash);
	}
	return hash_fmix32(hash);
}

bool WRecord::recursive_equal(const WRecord &p_other, int p_depth) const {
	if (data == p_other.data) {
		return true;
	}
	if (!data || !p_other.data || data->schema != p_other.data->schema || p_depth > Variant::MAX_RECURSION_DEPTH) {
		return false;
	}
	for (int index = 0; index < data->fields.size(); ++index) {
		if (!data->fields[index].hash_compare(p_other.data->fields[index], p_depth + 1)) {
			return false;
		}
	}
	return true;
}

WRecord::operator String() const {
	if (!data) {
		return "WRecord()";
	}
	return String(data->schema->get_name()) + " { " + itos(data->fields.size()) + " fields }";
}

WRecord::WRecord(const Ref<WRecordSchema> &p_schema, const Vector<Variant> &p_fields) {
	ERR_FAIL_COND(p_schema.is_null() || !p_schema->is_sealed() || p_schema->field_count() != p_fields.size());
	data = memnew(WRecordData(p_schema, p_fields));
}

WRecord WRecord::create(const Ref<WRecordSchema> &p_schema, const Dictionary &p_fields, bool &r_valid) {
	r_valid = false;
	if (construction_depth >= Variant::MAX_RECURSION_DEPTH) {
		ERR_PRINT("Record construction exceeded maximum nesting depth.");
		return WRecord();
	}
	struct DepthScope {
		DepthScope() { ++construction_depth; }
		~DepthScope() { --construction_depth; }
	} depth_scope;
	if (p_schema.is_null() || !p_schema->is_sealed()) {
		return WRecord();
	}
	for (const KeyValue<Variant, Variant> &field : p_fields) {
		if ((field.key.get_type() != Variant::STRING_NAME && field.key.get_type() != Variant::STRING) ||
				p_schema->find_field(field.key) < 0) {
			return WRecord();
		}
	}
	Vector<Variant> fields;
	fields.resize(p_schema->field_count());
	for (int index = 0; index < fields.size(); ++index) {
		const WRecordSchema::Field &field = p_schema->field(index);
		const Variant *literal = p_fields.getptr(field.name);
		fields.write[index] = field.type.instantiate(literal ? *literal : p_schema->default_literal(index), r_valid);
		if (!r_valid) {
			return WRecord();
		}
	}
	r_valid = true;
	return WRecord(p_schema, fields);
}

WRecord::WRecord(const WRecord &p_other) : data(p_other.data) {
	retain();
}

WRecord::WRecord(WRecord &&p_other) noexcept : data(p_other.data) {
	p_other.data = nullptr;
}

WRecord &WRecord::operator=(const WRecord &p_other) {
	if (data != p_other.data) {
		p_other.retain();
		release();
		data = p_other.data;
	}
	return *this;
}

WRecord &WRecord::operator=(WRecord &&p_other) noexcept {
	if (this != &p_other) {
		release();
		data = p_other.data;
		p_other.data = nullptr;
	}
	return *this;
}

WRecord::~WRecord() {
	release();
}

void WRecordSchemas::publish(const Ref<WRecordSchema> &p_schema) {
	ERR_FAIL_COND(p_schema.is_null() || !p_schema->is_sealed());
	SchemaEntry previous;
	Vector<WRecord> old_defaults;
	{
		RWLockWrite lock(schema_lock);
		SchemaEntry &entry = schemas[p_schema->get_name()];
		if (entry.schema == p_schema) {
			return;
		}
		ERR_FAIL_COND_MSG(entry.schema.is_valid() && entry.schema->get_source() != p_schema->get_source(), "Record name is already owned by another declaration.");
		previous = entry;
		entry = { p_schema, WRecord() };
		++schema_epoch;
		for (KeyValue<StringName, SchemaEntry> &item : schemas) {
			old_defaults.push_back(std::move(item.value.defaults));
		}
	}
}

Ref<WRecordSchema> WRecordSchemas::find(const StringName &p_name) {
	RWLockRead lock(schema_lock);
	const SchemaEntry *entry = schemas.getptr(p_name);
	return entry ? entry->schema : Ref<WRecordSchema>();
}

Ref<WRecordSchema> WRecordSchemas::resolve(const StringName &p_name) {
	Ref<WRecordSchema> schema = find(p_name);
	if (schema.is_null()) {
		const Ref<Script> script = WGodotNamedTypes::script(p_name);
		if (script.is_valid()) {
			schema = script->wgodot_get_record_schema();
		}
	}
	return schema;
}

WRecord WRecordSchemas::default_value(const StringName &p_name, bool &r_valid) {
	r_valid = false;
	Ref<WRecordSchema> schema = resolve(p_name);
	uint64_t epoch;
	{
		RWLockRead lock(schema_lock);
		epoch = schema_epoch;
		const SchemaEntry *entry = schemas.getptr(p_name);
		if (!entry) {
			return WRecord();
		}
		if (!entry->defaults.is_empty()) {
			r_valid = true;
			return entry->defaults;
		}
		schema = entry->schema;
	}
	if (constructing_defaults.has(p_name)) {
		ERR_PRINT("Recursive record default construction: " + String(p_name));
		return WRecord();
	}
	constructing_defaults.insert(p_name);
	WRecord value = WRecord::create(schema, Dictionary(), r_valid);
	constructing_defaults.erase(p_name);
	if (!r_valid) {
		return WRecord();
	}
	{
		RWLockWrite lock(schema_lock);
		SchemaEntry *entry = schemas.getptr(p_name);
		if (entry && entry->schema == schema && epoch == schema_epoch) {
			if (entry->defaults.is_empty()) {
				entry->defaults = value;
			}
			return entry->defaults;
		}
	}
	return value;
}

void WRecordSchemas::clear() {
	HashMap<StringName, SchemaEntry> previous;
	{
		RWLockWrite lock(schema_lock);
		SWAP(previous, schemas);
	}
}

Variant::Variant(const WRecord &p_record) : type(WRECORD) {
	static_assert(sizeof(WRecord) <= sizeof(_data._mem));
	memnew_placement(_data._mem, WRecord(p_record));
}

Variant::operator WRecord() const {
	return type == WRECORD ? *reinterpret_cast<const WRecord *>(_data._mem) : WRecord();
}

Ref<WRecordSchema> Script::wgodot_get_record_schema() const {
	return Ref<WRecordSchema>();
}
