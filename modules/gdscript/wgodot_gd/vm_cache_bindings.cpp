// wgodot-changes::file
#include "vm_cache_bindings.h"

#include "core/os/mutex.h"
#include "core/object/method_info.h"
#include "core/templates/hash_map.h"
#include "modules/gdscript/gdscript_utility_functions.h"

namespace WGodotVMCacheBindings {
namespace {
template <typename... T>
Array make(const T &...p_values) {
	return Array{ p_values... };
}

Mutex mutex;
bool initialized = false;
HashMap<uint64_t, Array> bindings[KIND_MAX];

template <typename T>
void add(Kind p_kind, T p_function, const Array &p_descriptor) {
	if (p_function) {
		bindings[p_kind].insert(reinterpret_cast<uint64_t>(p_function), p_descriptor);
	}
}

void initialize() {
	if (initialized) {
		return;
	}
	initialized = true;
	for (int type = 0; type < Variant::VARIANT_MAX; ++type) {
		const auto builtin = Variant::Type(type);
		for (int other = 0; other < Variant::VARIANT_MAX; ++other) {
			for (int operation = 0; operation < Variant::OP_MAX; ++operation) {
				add(OPERATOR, Variant::get_validated_operator_evaluator(Variant::Operator(operation), builtin, Variant::Type(other)), make(operation, type, other));
			}
		}
		List<StringName> members;
		Variant::get_member_list(builtin, &members);
		for (const StringName &member : members) {
			add(SETTER, Variant::get_member_validated_setter(builtin, member), make(type, member));
			add(GETTER, Variant::get_member_validated_getter(builtin, member), make(type, member));
		}
		if (Variant::has_indexing(builtin)) {
			add(INDEXED_SETTER, Variant::get_member_validated_indexed_setter(builtin), make(type));
			add(INDEXED_GETTER, Variant::get_member_validated_indexed_getter(builtin), make(type));
		}
		if (Variant::is_keyed(builtin)) {
			add(KEYED_SETTER, Variant::get_member_validated_keyed_setter(builtin), make(type));
			add(KEYED_GETTER, Variant::get_member_validated_keyed_getter(builtin), make(type));
		}
		List<StringName> methods;
		Variant::get_builtin_method_list(builtin, &methods);
		for (const StringName &method : methods) {
			add(BUILTIN_METHOD, Variant::get_validated_builtin_method(builtin, method), make(type, method));
		}
		List<MethodInfo> constructors;
		Variant::get_constructor_list(builtin, &constructors);
		for (int index = 0; index < constructors.size(); ++index) {
			add(CONSTRUCTOR, Variant::get_validated_constructor(builtin, index), make(type, index));
		}
	}
	List<StringName> utilities;
	Variant::get_utility_function_list(&utilities);
	for (const StringName &name : utilities) {
		add(UTILITY, Variant::get_validated_utility_function(name), make(name));
	}
	utilities.clear();
	GDScriptUtilityFunctions::get_function_list(&utilities);
	for (const StringName &name : utilities) {
		add(GDSCRIPT_UTILITY, GDScriptUtilityFunctions::get_function(name), make(name));
	}
}
} // namespace

Array describe(Kind p_kind, uint64_t p_address) {
	MutexLock lock(mutex);
	initialize();
	const Array *descriptor = bindings[p_kind].getptr(p_address);
	return descriptor ? *descriptor : Array();
}

uint64_t resolve(Kind p_kind, const Array &p_descriptor) {
	if (p_descriptor.is_empty()) {
		return 0;
	}
	switch (p_kind) {
		case OPERATOR:
			return reinterpret_cast<uint64_t>(Variant::get_validated_operator_evaluator(Variant::Operator(int(p_descriptor[0])), Variant::Type(int(p_descriptor[1])), Variant::Type(int(p_descriptor[2]))));
		case SETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_setter(Variant::Type(int(p_descriptor[0])), p_descriptor[1]));
		case GETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_getter(Variant::Type(int(p_descriptor[0])), p_descriptor[1]));
		case KEYED_SETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_keyed_setter(Variant::Type(int(p_descriptor[0]))));
		case KEYED_GETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_keyed_getter(Variant::Type(int(p_descriptor[0]))));
		case INDEXED_SETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_indexed_setter(Variant::Type(int(p_descriptor[0]))));
		case INDEXED_GETTER:
			return reinterpret_cast<uint64_t>(Variant::get_member_validated_indexed_getter(Variant::Type(int(p_descriptor[0]))));
		case BUILTIN_METHOD:
			return reinterpret_cast<uint64_t>(Variant::get_validated_builtin_method(Variant::Type(int(p_descriptor[0])), p_descriptor[1]));
		case CONSTRUCTOR:
			return reinterpret_cast<uint64_t>(Variant::get_validated_constructor(Variant::Type(int(p_descriptor[0])), p_descriptor[1]));
		case UTILITY:
			return reinterpret_cast<uint64_t>(Variant::get_validated_utility_function(p_descriptor[0]));
		case GDSCRIPT_UTILITY:
			return reinterpret_cast<uint64_t>(GDScriptUtilityFunctions::get_function(p_descriptor[0]));
		case KIND_MAX:
			return 0;
	}
	return 0;
}

void clear() {
	MutexLock lock(mutex);
	for (auto &table : bindings) {
		table.clear();
	}
	initialized = false;
}
} // namespace WGodotVMCacheBindings
