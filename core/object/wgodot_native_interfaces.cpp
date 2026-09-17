// wgodot-changes::file
#include "wgodot_native_interfaces.h"

#include "class_db.h"
#include "script_language.h"

namespace {
struct Contract {
	StringName native_base;
	HashSet<StringName> parents;
	HashSet<StringName> implementations;
};

// Written during game-module initialization, then immutable until shutdown.
HashMap<StringName, Contract> &contracts() {
	static HashMap<StringName, Contract> values;
	return values;
}
} // namespace

void WGodotNativeInterfaces::add_contract(const StringName &p_name, const StringName &p_native_base, const Vector<StringName> &p_parents) {
	Contract &contract = contracts()[p_name];
	contract.native_base = p_native_base;
	for (const StringName &parent : p_parents) {
		contract.parents.insert(parent);
	}
}

void WGodotNativeInterfaces::add_implementation(const StringName &p_contract, const StringName &p_class) {
	Contract *contract = contracts().getptr(p_contract);
	ERR_FAIL_NULL(contract);
	if (contract->implementations.has(p_class)) {
		return;
	}
	contract->implementations.insert(p_class);
	for (const StringName &parent : contract->parents) {
		add_implementation(parent, p_class);
	}
}

bool WGodotNativeInterfaces::accepts(const StringName &p_class, const StringName &p_contract) {
	const Contract *contract = contracts().getptr(p_contract);
	if (!contract) {
		return false;
	}
	for (StringName type = p_class; !type.is_empty(); type = ClassDB::get_parent_class(type)) {
		if (contract->implementations.has(type)) {
			return true;
		}
	}
	return false;
}

bool WGodotNativeInterfaces::is_instance(Object *p_object, const StringName &p_type) {
	if (!p_object) {
		return false;
	}
	if (p_object->is_class(p_type) || accepts(p_object->get_class_name(), p_type)) {
		return true;
	}
	const Ref<Script> script = p_object->get_script();
	return script.is_valid() && script->wgodot_implements_interface(p_type);
}

bool WGodotNativeInterfaces::can_reference(const StringName &p_source, const StringName &p_target) {
	if (p_source == p_target) {
		return true;
	}
	const Contract *source = contracts().getptr(p_source);
	if (contracts().has(p_target)) {
		return source ? source->parents.has(p_target) : accepts(p_source, p_target);
	}
	return ClassDB::is_parent_class(source ? source->native_base : p_source, p_target);
}

void WGodotNativeInterfaces::clear() {
	contracts().clear();
}
