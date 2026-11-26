// wgodot-changes::file
#include "vm_cache.h"

#include "vm_cache_bindings.h"
#include "core/io/marshalls.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/wgodot_record.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"

namespace {
enum ValueKind {
	PLAIN,
	SCRIPT,
	NATIVE_CLASS,
	RESOURCE,
	ARRAY,
	DICTIONARY,
	RECORD,
	EMPTY_HANDLE,
	NULL_OBJECT,
};

PackedByteArray pack(const Variant &p_value, bool &r_valid) {
	int size = 0;
	if (encode_variant(p_value, nullptr, size, false) != OK) {
		r_valid = false;
		return {};
	}
	PackedByteArray bytes;
	bytes.resize(size);
	if (encode_variant(p_value, bytes.ptrw(), size, false) != OK) {
		r_valid = false;
		return {};
	}
	return bytes;
}

Variant unpack(const uint8_t *p_bytes, int p_size, Variant::Type p_type, bool &r_valid) {
	Variant result;
	int used = 0;
	if (decode_variant(result, p_bytes, p_size, &used, false) != OK || used != p_size || result.get_type() != p_type) {
		r_valid = false;
		return {};
	}
	return result;
}

template <typename T>
Array strings(const T &p_values) {
	Array result;
	for (const auto &value : p_values) {
		result.push_back(value);
	}
	return result;
}

PackedInt32Array integers(const Vector<int> &p_values) {
	PackedInt32Array result;
	result.resize(p_values.size());
	for (int index = 0; index < p_values.size(); ++index) {
		result.set(index, p_values[index]);
	}
	return result;
}
} // namespace

struct WGodotGDScriptVMCache::Writer {
	struct ScalarComparator {
		static bool compare(const Variant &p_left, const Variant &p_right) {
			return p_left.hash_compare(p_right);
		}
	};

	GDScript *root;
	bool valid = true;
	PackedByteArray values;
	PackedInt64Array value_spans;
	HashMap<const void *, int> arrays;
	HashMap<const void *, int> dictionaries;
	HashMap<Variant, int, HashMapHasherDefault, ScalarComparator> scalar_values;
	Vector<Variant> containers;
	HashMap<int, StringName> global_names;

	explicit Writer(GDScript *p_root) : root(p_root) {
		for (const auto &entry : GDScriptLanguage::get_singleton()->get_global_map()) {
			global_names.insert(entry.value, entry.key);
		}
	}

	Array script(Script *p_script) {
		if (!p_script) {
			return {};
		}
		GDScript *gdscript = Object::cast_to<GDScript>(p_script);
		const String path = gdscript ? gdscript->get_root_script()->get_script_path() : p_script->get_path();
		if (path.is_empty()) {
			// A live declaration without a source identity cannot be restored.
			valid = false;
			return {};
		}
		return Array{ path, gdscript ? gdscript->fully_qualified_name : String() };
	}

	Array container_type(const ContainerType &p_type) {
		return Array{ p_type.builtin_type, p_type.class_name, script(p_type.script.ptr()) };
	}

	int value(const Variant &p_value) {
		const Variant::Type kind = p_value.get_type();
		const bool scalar = kind == Variant::NIL || kind == Variant::BOOL || kind == Variant::INT ||
				kind == Variant::STRING || kind == Variant::STRING_NAME;
		if (scalar) {
			if (const int *found = scalar_values.getptr(p_value)) {
				return *found;
			}
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array array = p_value;
			if (const int *found = arrays.getptr(array.id())) {
				return *found;
			}
		} else if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary dictionary = p_value;
			if (const int *found = dictionaries.getptr(dictionary.id())) {
				return *found;
			}
		}
		const int index = value_spans.size() / 2;
		value_spans.resize(value_spans.size() + 2);
		if (scalar) {
			// Metadata repeats names, flags and integers on every field. Share
			// immutable scalars while retaining their exact Variant types.
			scalar_values.insert(p_value, index);
		}
		Array result;
		switch (p_value.get_type()) {
			case Variant::OBJECT: {
				Object *object = p_value.get_validated_object();
				if (!object) {
					result = Array{ NULL_OBJECT };
				} else if (Script *declaration = Object::cast_to<Script>(object)) {
					result = Array{ SCRIPT, script(declaration) };
				} else if (GDScriptNativeClass *native = Object::cast_to<GDScriptNativeClass>(object)) {
					result = Array{ NATIVE_CLASS, native->get_name() };
				} else if (Resource *resource = Object::cast_to<Resource>(object)) {
					const String path = resource->get_path();
					valid &= path.begins_with("res://");
					result = Array{ RESOURCE, path };
				} else {
					valid = false;
				}
			} break;
			case Variant::ARRAY: {
				const Array array = p_value;
				// Keep identity keys alive throughout encoding, including temporary
				// metadata containers whose addresses could otherwise be reused.
				containers.push_back(p_value);
				arrays.insert(array.id(), index);
				Array entries;
				for (const Variant &entry : array) {
					entries.push_back(value(entry));
				}
				result = Array{ ARRAY, container_type(array.get_element_type()), entries, array.is_read_only(), p_value.wgodot_is_value_container() };
			} break;
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_value;
				containers.push_back(p_value);
				dictionaries.insert(dictionary.id(), index);
				Array entries;
				for (const auto &entry : dictionary) {
					entries.push_back(Array{ value(entry.key), value(entry.value) });
				}
				result = Array{ DICTIONARY, container_type(dictionary.get_key_type()), container_type(dictionary.get_value_type()), entries, dictionary.is_read_only(), p_value.wgodot_is_value_container() };
			} break;
			case Variant::WRECORD: {
				const WRecord record = p_value;
				Array fields;
				if (record.is_empty()) {
					result = Array{ RECORD, String(), StringName(), fields };
				} else {
					for (int field = 0; field < record.schema()->field_count(); ++field) {
						fields.push_back(value(record.get(field)));
					}
					result = Array{ RECORD, record.schema()->get_source(), record.schema()->get_name(), fields };
				}
			} break;
			case Variant::CALLABLE:
			case Variant::SIGNAL:
			case Variant::RID: {
				// Process-owned handles are not executable constants on disk.
				const bool empty = (p_value.get_type() == Variant::CALLABLE && Callable(p_value).is_null()) ||
						(p_value.get_type() == Variant::SIGNAL && Signal(p_value).is_null()) ||
						(p_value.get_type() == Variant::RID && !RID(p_value).is_valid());
				valid &= empty;
				result = Array{ EMPTY_HANDLE, p_value.get_type() };
			} break;
			default:
				result = Array{ PLAIN, p_value, p_value.wgodot_is_value_container() };
				break;
		}
		const PackedByteArray encoded = pack(result, valid);
		value_spans.set(index * 2, values.size());
		value_spans.set(index * 2 + 1, encoded.size());
		values.append_array(encoded);
		return index;
	}

	Array type(const GDScriptDataType &p_type) {
		Array elements;
		for (const auto &element : p_type.container_element_types) {
			elements.push_back(type(element));
		}
		return Array{ p_type.kind, p_type.builtin_type, p_type.native_type, script(p_type.script_type), p_type.script_type_ref.is_valid(), p_type.wgodot_scalar, p_type.wgodot_value, p_type.wgodot_class_ref, elements, p_type.wgodot_resource_path };
	}

	Array record_type(const WRecordFieldType &p_type) {
		Array elements;
		for (const auto &element : p_type.elements) {
			elements.push_back(record_type(element));
		}
		Script *declaration = Object::cast_to<Script>(ObjectDB::get_instance(p_type.declaration));
		return Array{ p_type.builtin, p_type.class_name, p_type.record_name, script(declaration), p_type.scalar, p_type.value_container, elements };
	}

	Dictionary member(const GDScript::MemberInfo &p_member) {
		return Dictionary{
			{ "index", p_member.index },
			{ "setter", p_member.setter },
			{ "getter", p_member.getter },
			{ "type", type(p_member.data_type) },
			{ "property", value(Dictionary(p_member.property_info)) },
		};
	}

	template <typename T>
	Array bindings(WGodotVMCacheBindings::Kind p_kind, const Vector<T> &p_functions) {
		Array result;
		for (T function : p_functions) {
			const Array descriptor = WGodotVMCacheBindings::describe(p_kind, reinterpret_cast<uint64_t>(function));
			valid &= !descriptor.is_empty();
			result.push_back(descriptor);
		}
		return result;
	}

	Dictionary function(GDScriptFunction *p_function) {
		if (!p_function) {
			return {};
		}
		Dictionary result;
#define FIELD(m_name) result[#m_name] = p_function->m_name
		FIELD(name);
		FIELD(source);
		FIELD(_static);
		FIELD(wgodot_storage_arguments);
		FIELD(_initial_line);
		FIELD(_argument_count);
		FIELD(_vararg_index);
		FIELD(_stack_size);
		FIELD(_instruction_args_size);
#undef FIELD
		result["method_info"] = value(Dictionary(p_function->method_info));
		result["rpc_config"] = value(p_function->rpc_config);
		result["return_type"] = type(p_function->return_type);
		Array arguments;
		for (const auto &argument : p_function->argument_types) {
			arguments.push_back(type(argument));
		}
		result["argument_types"] = arguments;
		result["code"] = integers(p_function->code);
		result["default_arguments"] = integers(p_function->default_arguments);
		Array constants;
		for (const Variant &constant : p_function->constants) {
			constants.push_back(value(constant));
		}
		result["constants"] = constants;
		Dictionary named_constants;
		for (const auto &entry : p_function->constant_map) {
			named_constants[entry.key] = value(entry.value);
		}
		result["constant_map"] = named_constants;
		result["declared_constants"] = strings(p_function->wgodot_declared_local_constants);
		result["global_names"] = strings(p_function->global_names);
		Array globals;
		for (int operand : p_function->wgodot_global_operands) {
			const StringName *name = global_names.getptr(p_function->code[operand]);
			if (!name) {
				valid = false;
				continue;
			}
			globals.push_back(Array{ operand, *name });
		}
		result["globals"] = globals;
		Array temporaries;
		for (const auto &slot : p_function->temporary_slots) {
			temporaries.push_back(Array{ slot.first, slot.second });
		}
		result["temporary_slots"] = temporaries;
		Array debug;
		for (const auto &slot : p_function->stack_debug) {
			debug.push_back(Array{ slot.line, slot.pos, slot.added, slot.identifier });
		}
		result["stack_debug"] = debug;
#define BINDINGS(m_name, m_kind) result[#m_name] = bindings(WGodotVMCacheBindings::m_kind, p_function->m_name)
		BINDINGS(operator_funcs, OPERATOR);
		BINDINGS(setters, SETTER);
		BINDINGS(getters, GETTER);
		BINDINGS(keyed_setters, KEYED_SETTER);
		BINDINGS(keyed_getters, KEYED_GETTER);
		BINDINGS(indexed_setters, INDEXED_SETTER);
		BINDINGS(indexed_getters, INDEXED_GETTER);
		BINDINGS(builtin_methods, BUILTIN_METHOD);
		BINDINGS(constructors, CONSTRUCTOR);
		BINDINGS(utilities, UTILITY);
		BINDINGS(gds_utilities, GDSCRIPT_UTILITY);
#undef BINDINGS
		Array methods;
		for (const MethodBind *method : p_function->methods) {
			methods.push_back(Array{ method->get_instance_class(), method->get_name(), int64_t(method->get_hash()) });
		}
		result["methods"] = methods;
		Array lambdas;
		for (GDScriptFunction *lambda : p_function->lambdas) {
			Dictionary entry = function(lambda);
			const GDScript::LambdaInfo *info = p_function->_script->lambda_info.getptr(lambda);
			valid &= info != nullptr;
			if (info) {
				entry["capture_count"] = info->capture_count;
				entry["use_self"] = info->use_self;
			}
			lambdas.push_back(entry);
		}
		result["lambdas"] = lambdas;
#ifdef DEBUG_ENABLED
#define FIELD(m_name) result[#m_name] = strings(p_function->m_name)
		FIELD(operator_names);
		FIELD(setter_names);
		FIELD(getter_names);
		FIELD(builtin_methods_names);
		FIELD(constructors_names);
		FIELD(utilities_names);
		FIELD(gds_utilities_names);
#undef FIELD
		result["signature"] = p_function->profile.signature;
#endif
		return result;
	}

	PackedByteArray declaration(GDScript *p_script) {
		Dictionary result;
#define FIELD(m_name) result[#m_name] = p_script->m_name
		FIELD(tool);
		FIELD(_is_abstract);
		FIELD(local_name);
		FIELD(global_name);
		FIELD(fully_qualified_name);
		FIELD(simplified_icon_path);
		FIELD(wgodot_is_interface);
		FIELD(wgodot_interface_key);
		FIELD(wgodot_class_reference);
#undef FIELD
		result["native"] = p_script->native.is_valid() ? p_script->native->get_name() : StringName();
		result["base"] = script(p_script->base.ptr());
		result["members"] = strings(p_script->members);
		result["declared_constants"] = strings(p_script->wgodot_declared_constants);
		result["interfaces"] = strings(p_script->wgodot_implemented_interfaces);
		Dictionary indices;
		for (const auto &entry : p_script->member_indices) {
			indices[entry.key] = member(entry.value);
		}
		result["member_indices"] = indices;
		indices = Dictionary();
		for (const auto &entry : p_script->static_variables_indices) {
			indices[entry.key] = member(entry.value);
		}
		result["static_variables_indices"] = indices;
		Dictionary constants;
		for (const auto &entry : p_script->constants) {
			constants[entry.key] = value(entry.value);
		}
		result["constants"] = constants;
		result["rpc_config"] = value(p_script->rpc_config);
		Dictionary signals;
		for (const auto &entry : p_script->_signals) {
			signals[entry.key] = value(Dictionary(entry.value));
		}
		result["signals"] = signals;
		Dictionary methods;
		for (const auto &entry : p_script->wgodot_default_methods) {
			methods[entry.key] = type(entry.value);
		}
		result["default_methods"] = methods;
		Dictionary annotations;
		for (const auto &entry : p_script->wgodot_field_annotations) {
			annotations[entry.key] = value(entry.value);
		}
		result["field_annotations"] = annotations;
		result["class_annotations"] = value(p_script->wgodot_class_annotations);
		Array functions;
		for (const auto &entry : p_script->member_functions) {
			functions.push_back(function(entry.value));
		}
		result["functions"] = functions;
		result["implicit_initializer"] = function(p_script->implicit_initializer);
		result["implicit_ready"] = function(p_script->implicit_ready);
		result["static_initializer"] = function(p_script->static_initializer);
		result["initializer"] = p_script->initializer ? p_script->initializer->name : StringName();
		Dictionary subclasses;
		for (const auto &entry : p_script->subclasses) {
			subclasses[entry.key] = declaration(entry.value.ptr());
		}
		result["subclasses"] = subclasses;
		Array record;
		if (p_script->wgodot_record_schema.is_valid()) {
			const Ref<WRecordSchema> schema = p_script->wgodot_record_schema;
			Array fields;
			for (int index = 0; index < schema->field_count(); ++index) {
				fields.push_back(Array{ schema->field(index).name, record_type(schema->field(index).type), value(schema->default_literal(index)) });
			}
			record = Array{ schema->get_name(), schema->get_source(), fields };
		}
		result["record"] = record;
#ifdef TOOLS_ENABLED
		Dictionary member_lines;
		for (const auto &entry : p_script->member_lines) {
			member_lines[entry.key] = entry.value;
		}
		result["member_lines"] = member_lines;
		Dictionary defaults;
		for (const auto &entry : p_script->member_default_values) {
			defaults[entry.key] = value(entry.value);
		}
		result["member_default_values"] = defaults;
		defaults = Dictionary();
		for (const auto &entry : p_script->member_default_values_cache) {
			defaults[entry.key] = value(entry.value);
		}
		result["member_default_values_cache"] = defaults;
		Array properties;
		for (const auto &property : p_script->members_cache) {
			properties.push_back(value(Dictionary(property)));
		}
		result["members_cache"] = properties;
		result["exports_cached"] = !p_script->source_changed_cache;
		result["doc_class_name"] = p_script->doc_class_name;
		result["doc"] = DocData::ClassDoc::to_dict(p_script->doc);
		Array docs;
		for (const auto &doc : p_script->docs) {
			docs.push_back(DocData::ClassDoc::to_dict(doc));
		}
		result["docs"] = docs;
#endif
		// Keep each class compact until it is restored. A namespace may contain
		// thousands of classes; expanding all metadata together dwarfs the VM.
		return pack(result, valid);
	}
};

Dictionary WGodotGDScriptVMCache::encode(GDScript *p_script) {
	Writer writer(p_script);
	Dictionary result;
	result["class"] = writer.declaration(p_script);
	result["values"] = writer.values;
	result["value_spans"] = writer.value_spans;
	return writer.valid ? result : Dictionary();
}

struct WGodotGDScriptVMCache::Reader {
	GDScript *root;
	bool valid = true;
	PackedByteArray encoded_values;
	PackedInt64Array value_spans;
	Vector<Variant> values;
	Vector<bool> decoded;

	Reader(GDScript *p_root, const Dictionary &p_snapshot) : root(p_root) {
		encoded_values = p_snapshot["values"];
		value_spans = p_snapshot["value_spans"];
		valid = value_spans.size() % 2 == 0;
		values.resize(value_spans.size() / 2);
		decoded.resize(values.size());
		decoded.fill(false);
	}

	Ref<Script> script(const Array &p_reference) {
		if (p_reference.is_empty()) {
			return {};
		}
		const String path = p_reference[0];
		const String qualified_name = p_reference[1];
		if (path.is_empty()) {
			// Older snapshots may contain an unaddressable declaration. Treat
			// this as a cache miss before ResourceLoader resolves it to res://.
			valid = false;
			return {};
		}
		if (path == root->path) {
			GDScript *found = root->find_class(qualified_name);
			valid &= found != nullptr;
			return Ref<GDScript>(found);
		}
		if (path.get_extension() == "gd" || path.get_extension() == "gdc") {
			Error error = OK;
			Ref<GDScript> loaded = GDScriptCache::get_shallow_script(path, error, root->path);
			if (error != OK || loaded.is_null()) {
				valid = false;
				return {};
			}
			GDScript *found = loaded->find_class(qualified_name);
			valid &= found != nullptr;
			return Ref<GDScript>(found);
		}
		Ref<Script> loaded = ResourceLoader::load(path, "Script");
		valid &= loaded.is_valid();
		return loaded;
	}

	ContainerType container_type(const Array &p_type) {
		ContainerType result;
		result.builtin_type = Variant::Type(int(p_type[0]));
		result.class_name = p_type[1];
		result.script = script(p_type[2]);
		return result;
	}

	Variant value(int p_index) {
		if (p_index < 0 || p_index >= values.size()) {
			valid = false;
			return {};
		}
		if (decoded[p_index]) {
			return values[p_index];
		}
		decoded.write[p_index] = true;
		const int64_t offset = value_spans[p_index * 2];
		const int64_t length = value_spans[p_index * 2 + 1];
		if (offset < 0 || length < 0 || offset > encoded_values.size() || length > encoded_values.size() - offset) {
			valid = false;
			return {};
		}
		const Array entry = unpack(encoded_values.ptr() + offset, length, Variant::ARRAY, valid);
		if (!valid || entry.is_empty()) {
			return {};
		}
		Variant result;
		switch (int(entry[0])) {
			case NULL_OBJECT:
				// A null Object constant is distinct from a NIL Variant in the
				// VM's typed-container checks, even though both print as null.
				result = static_cast<Object *>(nullptr);
				break;
			case PLAIN:
				result = entry[1];
				if (entry[2]) {
					result.wgodot_make_value_container();
				}
				break;
			case EMPTY_HANDLE: {
				Callable::CallError error;
				Variant::construct(Variant::Type(int(entry[1])), result, nullptr, 0, error);
				valid &= error.error == Callable::CallError::CALL_OK;
			} break;
			case SCRIPT:
				result = script(entry[1]);
				break;
			case NATIVE_CLASS: {
				const StringName name = entry[1];
				const int *index = GDScriptLanguage::get_singleton()->get_global_map().getptr(name);
				if (index) {
					result = GDScriptLanguage::get_singleton()->get_global_array()[*index];
				} else {
					valid = false;
				}
			} break;
			case RESOURCE: {
				Ref<Resource> resource = ResourceLoader::load(entry[1]);
				valid &= resource.is_valid();
				result = resource;
			} break;
			case ARRAY: {
				Array array;
				array.set_typed(container_type(entry[1]));
				values.write[p_index] = array;
				const Array items = entry[2];
				for (const Variant &item : items) {
					array.push_back(value(item));
				}
				if (entry[3]) {
					array.make_read_only();
				}
				result = array;
				if (entry[4]) {
					result.wgodot_make_value_container();
				}
			} break;
			case DICTIONARY: {
				Dictionary dictionary;
				dictionary.set_typed(container_type(entry[1]), container_type(entry[2]));
				values.write[p_index] = dictionary;
				const Array items = entry[3];
				for (const Array item : items) {
					dictionary[value(item[0])] = value(item[1]);
				}
				if (entry[4]) {
					dictionary.make_read_only();
				}
				result = dictionary;
				if (entry[5]) {
					result.wgodot_make_value_container();
				}
			} break;
			case RECORD: {
				const String path = entry[1];
				if (path.is_empty()) {
					result = WRecord();
					break;
				}
				if (path != root->path) {
					Error error = OK;
					GDScriptCache::get_full_script(path, error, root->path);
					valid &= error == OK;
				}
				Ref<WRecordSchema> schema = WRecordSchemas::find(entry[2]);
				if (schema.is_null()) {
					valid = false;
					break;
				}
				Vector<Variant> fields;
				const Array items = entry[3];
				for (const Variant &item : items) {
					fields.push_back(value(item));
				}
				valid &= fields.size() == schema->field_count();
				if (valid) {
					result = WRecord(schema, fields);
				}
			} break;
			default:
				valid = false;
				break;
		}
		values.write[p_index] = result;
		return result;
	}

	GDScriptDataType type(const Array &p_type) {
		GDScriptDataType result;
		result.kind = GDScriptDataType::Kind(int(p_type[0]));
		result.builtin_type = Variant::Type(int(p_type[1]));
		result.native_type = p_type[2];
		Ref<Script> declaration = script(p_type[3]);
		result.script_type = declaration.ptr();
		if (p_type[4]) {
			result.script_type_ref = declaration;
		}
		result.wgodot_scalar = WGodotScalar::Type(int(p_type[5]));
		result.wgodot_value = p_type[6];
		result.wgodot_class_ref = p_type[7];
		result.wgodot_resource_path = p_type[9];
		const Array elements = p_type[8];
		for (const Array element : elements) {
			result.container_element_types.push_back(type(element));
		}
		return result;
	}

	WRecordFieldType record_type(const Array &p_type) {
		WRecordFieldType result;
		result.builtin = Variant::Type(int(p_type[0]));
		result.class_name = p_type[1];
		result.record_name = p_type[2];
		Ref<Script> declaration = script(p_type[3]);
		if (declaration.is_valid()) {
			result.declaration = declaration->get_instance_id();
		}
		result.scalar = WGodotScalar::Type(int(p_type[4]));
		result.value_container = p_type[5];
		const Array elements = p_type[6];
		for (const Array element : elements) {
			result.elements.push_back(record_type(element));
		}
		return result;
	}

	GDScript::MemberInfo member(const Dictionary &p_member) {
		GDScript::MemberInfo result;
		result.index = p_member["index"];
		result.setter = p_member["setter"];
		result.getter = p_member["getter"];
		result.data_type = type(p_member["type"]);
		result.property_info = PropertyInfo::from_dict(value(p_member["property"]));
		return result;
	}

	template <typename T>
	void bindings(Vector<T> &r_functions, WGodotVMCacheBindings::Kind p_kind, const Array &p_descriptors) {
		for (const Array descriptor : p_descriptors) {
			T function = reinterpret_cast<T>(WGodotVMCacheBindings::resolve(p_kind, descriptor));
			valid &= function != nullptr;
			r_functions.push_back(function);
		}
	}

	GDScriptFunction *function(GDScript *p_script, const Dictionary &p_function) {
		if (p_function.is_empty()) {
			return nullptr;
		}
		GDScriptFunction *result = memnew(GDScriptFunction);
		result->_script = p_script;
#define FIELD(m_name) result->m_name = p_function[#m_name]
		FIELD(name);
		FIELD(source);
		FIELD(_static);
		FIELD(wgodot_storage_arguments);
		FIELD(_initial_line);
		FIELD(_argument_count);
		FIELD(_vararg_index);
		FIELD(_stack_size);
		FIELD(_instruction_args_size);
#undef FIELD
		result->method_info = MethodInfo::from_dict(value(p_function["method_info"]));
		result->rpc_config = value(p_function["rpc_config"]);
		result->return_type = type(p_function["return_type"]);
		const Array arguments = p_function["argument_types"];
		for (const Array argument : arguments) {
			result->argument_types.push_back(type(argument));
		}
		const PackedInt32Array code = p_function["code"];
		for (int instruction : code) {
			result->code.push_back(instruction);
		}
		const PackedInt32Array default_arguments = p_function["default_arguments"];
		for (int argument : default_arguments) {
			result->default_arguments.push_back(argument);
		}
		const Array constants = p_function["constants"];
		for (const Variant &constant : constants) {
			result->constants.push_back(value(constant));
		}
		const Dictionary named_constants = p_function["constant_map"];
		for (const auto &entry : named_constants) {
			result->constant_map.insert(entry.key, value(entry.value));
		}
		const Array declared_constants = p_function["declared_constants"];
		for (const StringName name : declared_constants) {
			result->wgodot_declared_local_constants.insert(name);
		}
		const Array names = p_function["global_names"];
		for (const StringName name : names) {
			result->global_names.push_back(name);
		}
		const Array globals = p_function["globals"];
		for (const Array entry : globals) {
			const int operand = entry[0];
			const int *index = GDScriptLanguage::get_singleton()->get_global_map().getptr(entry[1]);
			if (!index || operand < 0 || operand >= result->code.size()) {
				valid = false;
				continue;
			}
			result->code.write[operand] = *index;
			result->wgodot_global_operands.push_back(operand);
		}
		const Array temporaries = p_function["temporary_slots"];
		for (const Array slot : temporaries) {
			result->temporary_slots.push_back(Pair<int, Variant::Type>(slot[0], Variant::Type(int(slot[1]))));
		}
		const Array debug = p_function["stack_debug"];
		for (const Array slot : debug) {
			result->stack_debug.push_back({ int(slot[0]), int(slot[1]), bool(slot[2]), StringName(slot[3]) });
		}
#define BINDINGS(m_name, m_kind) bindings(result->m_name, WGodotVMCacheBindings::m_kind, p_function[#m_name])
		BINDINGS(operator_funcs, OPERATOR);
		BINDINGS(setters, SETTER);
		BINDINGS(getters, GETTER);
		BINDINGS(keyed_setters, KEYED_SETTER);
		BINDINGS(keyed_getters, KEYED_GETTER);
		BINDINGS(indexed_setters, INDEXED_SETTER);
		BINDINGS(indexed_getters, INDEXED_GETTER);
		BINDINGS(builtin_methods, BUILTIN_METHOD);
		BINDINGS(constructors, CONSTRUCTOR);
		BINDINGS(utilities, UTILITY);
		BINDINGS(gds_utilities, GDSCRIPT_UTILITY);
#undef BINDINGS
		const Array methods = p_function["methods"];
		for (const Array entry : methods) {
			const MethodBind *method = ClassDB::get_method(entry[0], entry[1]);
			valid &= method && method->get_hash() == uint32_t(int64_t(entry[2]));
			result->methods.push_back(method);
		}
		const Array lambdas = p_function["lambdas"];
		for (const Dictionary entry : lambdas) {
			GDScriptFunction *lambda = function(p_script, entry);
			result->lambdas.push_back(lambda);
			p_script->lambda_info.insert(lambda, { int(entry["capture_count"]), bool(entry["use_self"]) });
		}
		// Pointer views are rebuilt only after the owning vectors are populated.
#define VIEW(m_vector, m_count, m_pointer) \
		result->m_count = result->m_vector.size(); \
		result->m_pointer = result->m_vector.ptrw()
		VIEW(code, _code_size, _code_ptr);
		VIEW(constants, _constant_count, _constants_ptr);
		VIEW(global_names, _global_names_count, _global_names_ptr);
		VIEW(operator_funcs, _operator_funcs_count, _operator_funcs_ptr);
		VIEW(setters, _setters_count, _setters_ptr);
		VIEW(getters, _getters_count, _getters_ptr);
		VIEW(keyed_setters, _keyed_setters_count, _keyed_setters_ptr);
		VIEW(keyed_getters, _keyed_getters_count, _keyed_getters_ptr);
		VIEW(indexed_setters, _indexed_setters_count, _indexed_setters_ptr);
		VIEW(indexed_getters, _indexed_getters_count, _indexed_getters_ptr);
		VIEW(builtin_methods, _builtin_methods_count, _builtin_methods_ptr);
		VIEW(constructors, _constructors_count, _constructors_ptr);
		VIEW(utilities, _utilities_count, _utilities_ptr);
		VIEW(gds_utilities, _gds_utilities_count, _gds_utilities_ptr);
		VIEW(methods, _methods_count, _methods_ptr);
		VIEW(lambdas, _lambdas_count, _lambdas_ptr);
#undef VIEW
		result->_default_arg_count = MAX(0, result->default_arguments.size() - 1);
		result->_default_arg_ptr = result->default_arguments.ptr();
#ifdef DEBUG_ENABLED
#define FIELD(m_name) \
		for (const String entry : Array(p_function[#m_name])) { \
			result->m_name.push_back(entry); \
		}
		FIELD(operator_names);
		FIELD(setter_names);
		FIELD(getter_names);
		FIELD(builtin_methods_names);
		FIELD(constructors_names);
		FIELD(utilities_names);
		FIELD(gds_utilities_names);
#undef FIELD
		result->profile.signature = p_function["signature"];
		result->func_cname = (String(result->source) + " - " + String(result->name)).utf8();
		result->_func_cname = result->func_cname.get_data();
#endif
		return result;
	}

	void shape(GDScript *p_script, const PackedByteArray &p_encoded) {
		const Dictionary p_class = unpack(p_encoded.ptr(), p_encoded.size(), Variant::DICTIONARY, valid);
		if (!valid) {
			return;
		}
#define FIELD(m_name) p_script->m_name = p_class[#m_name]
		FIELD(tool);
		FIELD(_is_abstract);
		FIELD(local_name);
		FIELD(global_name);
		FIELD(fully_qualified_name);
		FIELD(simplified_icon_path);
		FIELD(wgodot_is_interface);
		FIELD(wgodot_interface_key);
		FIELD(wgodot_class_reference);
#undef FIELD
		for (const String name : Array(p_class["interfaces"])) {
			p_script->wgodot_implemented_interfaces.insert(name);
		}
		const StringName native_name = p_class["native"];
		if (native_name != StringName()) {
			const int *index = GDScriptLanguage::get_singleton()->get_global_map().getptr(native_name);
			if (index) {
				p_script->native = GDScriptLanguage::get_singleton()->get_global_array()[*index];
			} else {
				valid = false;
			}
		}
		const Dictionary children = p_class["subclasses"];
		for (const auto &child : children) {
			Ref<GDScript> declaration;
			declaration.instantiate();
			declaration->_owner = p_script;
			declaration->path = root->path;
			p_script->subclasses.insert(child.key, declaration);
			shape(declaration.ptr(), child.value);
		}
	}

	void declaration(GDScript *p_script, const PackedByteArray &p_encoded) {
		const Dictionary p_class = unpack(p_encoded.ptr(), p_encoded.size(), Variant::DICTIONARY, valid);
		if (!valid) {
			return;
		}
		p_script->base = script(p_class["base"]);
		if (p_script->base.is_valid() && p_script->base->get_root_script() != root) {
			Error error = OK;
			GDScriptCache::get_full_script(p_script->base->get_root_script()->path, error, root->path);
			valid &= error == OK;
		}
		const Dictionary indices = p_class["member_indices"];
		for (const auto &entry : indices) {
			GDScript::MemberInfo info = member(entry.value);
			p_script->member_indices.insert(entry.key, info);
			if (info.data_type.wgodot_scalar != WGodotScalar::NONE) {
				p_script->wgodot_scalar_members.insert(info.index, info.data_type.wgodot_scalar);
			}
		}
		const Dictionary static_indices = p_class["static_variables_indices"];
		for (const auto &entry : static_indices) {
			p_script->static_variables_indices.insert(entry.key, member(entry.value));
		}
		p_script->static_variables.resize(p_script->static_variables_indices.size());
		for (const StringName name : Array(p_class["members"])) {
			p_script->members.insert(name);
		}
		for (const StringName name : Array(p_class["declared_constants"])) {
			p_script->wgodot_declared_constants.insert(name);
		}
		for (const String name : Array(p_class["interfaces"])) {
			p_script->wgodot_implemented_interfaces.insert(name);
		}
		const Dictionary default_methods = p_class["default_methods"];
		for (const auto &entry : default_methods) {
			p_script->wgodot_default_methods.insert(entry.key, type(entry.value));
		}
		const Array record = p_class["record"];
		if (!record.is_empty()) {
			Vector<WRecordSchema::Field> fields;
			Vector<Variant> defaults;
			for (const Array field : Array(record[2])) {
				fields.push_back({ StringName(field[0]), record_type(field[1]) });
				defaults.push_back(value(field[2]));
			}
			p_script->wgodot_record_schema.instantiate();
			p_script->wgodot_record_schema->initialize(record[0], record[1], fields, defaults);
			valid &= p_script->wgodot_record_schema->is_sealed();
			WRecordSchemas::publish(p_script->wgodot_record_schema);
		}
		const Dictionary children = p_class["subclasses"];
		for (const auto &entry : children) {
			declaration(p_script->subclasses[entry.key].ptr(), entry.value);
		}
		const Dictionary constants = p_class["constants"];
		for (const auto &entry : constants) {
			p_script->constants.insert(entry.key, value(entry.value));
		}
		p_script->rpc_config = value(p_class["rpc_config"]);
		const Dictionary signals = p_class["signals"];
		for (const auto &entry : signals) {
			p_script->_signals.insert(entry.key, MethodInfo::from_dict(value(entry.value)));
		}
		const Dictionary annotations = p_class["field_annotations"];
		for (const auto &entry : annotations) {
			p_script->wgodot_field_annotations.insert(entry.key, value(entry.value));
		}
		p_script->wgodot_class_annotations = value(p_class["class_annotations"]);
		const Array functions = p_class["functions"];
		for (const Dictionary entry : functions) {
			GDScriptFunction *restored = function(p_script, entry);
			p_script->member_functions.insert(restored->name, restored);
		}
		p_script->implicit_initializer = function(p_script, p_class["implicit_initializer"]);
		p_script->implicit_ready = function(p_script, p_class["implicit_ready"]);
		p_script->static_initializer = function(p_script, p_class["static_initializer"]);
		const StringName initializer = p_class["initializer"];
		if (initializer != StringName()) {
			GDScriptFunction **found = p_script->member_functions.getptr(initializer);
			valid &= found != nullptr;
			p_script->initializer = found ? *found : nullptr;
		}
#ifdef TOOLS_ENABLED
		const Dictionary lines = p_class["member_lines"];
		for (const auto &entry : lines) {
			p_script->member_lines.insert(entry.key, entry.value);
		}
		const Dictionary defaults = p_class["member_default_values"];
		for (const auto &entry : defaults) {
			p_script->member_default_values.insert(entry.key, value(entry.value));
		}
		const Dictionary cached_defaults = p_class["member_default_values_cache"];
		for (const auto &entry : cached_defaults) {
			p_script->member_default_values_cache.insert(entry.key, value(entry.value));
		}
		for (const Variant &property : Array(p_class["members_cache"])) {
			p_script->members_cache.push_back(PropertyInfo::from_dict(value(property)));
		}
		p_script->source_changed_cache = !bool(p_class["exports_cached"]);
		p_script->base_cache = p_script->base;
		if (p_script->base_cache.is_valid()) {
			p_script->base_cache->inheriters_cache.insert(p_script->get_instance_id());
		}
		p_script->doc_class_name = p_class["doc_class_name"];
		p_script->doc = DocData::ClassDoc::from_dict(p_class["doc"]);
		for (const Dictionary doc : Array(p_class["docs"])) {
			p_script->docs.push_back(DocData::ClassDoc::from_dict(doc));
		}
#endif
		p_script->_static_default_init();
		p_script->valid = valid;
	}
};

bool WGodotGDScriptVMCache::decode(GDScript *p_script, const Dictionary &p_snapshot, bool p_shape_only) {
	Reader reader(p_script, p_snapshot);
	if (p_shape_only) {
		reader.shape(p_script, p_snapshot["class"]);
	} else {
		reader.declaration(p_script, p_snapshot["class"]);
	}
	return reader.valid;
}
