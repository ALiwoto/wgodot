// wgodot-changes::file
#include "wgodot_interface_registry.h"

#include "class_db.h"
#include "wgodot_native_interfaces.h"

namespace {
HashMap<StringName, WGodotNativeInterfaces::Descriptor> &descriptors() {
	static HashMap<StringName, WGodotNativeInterfaces::Descriptor> values;
	return values;
}
} // namespace

void WGodotNativeInterfaces::Builder::import_api(const StringName &p_class) {
	List<MethodInfo> methods;
	ClassDB::get_method_list(p_class, &methods, true);
	for (const MethodInfo &entry : methods) {
		method(entry);
	}
	List<PropertyInfo> properties;
	ClassDB::get_property_list(p_class, &properties, true);
	for (PropertyInfo entry : properties) {
		if (entry.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			continue;
		}
		if (entry.type == Variant::OBJECT && entry.class_name.is_empty()) {
			entry.class_name = entry.hint_string;
		}
		descriptor.properties.insert(entry.name, { entry, ClassDB::get_property_getter(p_class, entry.name), ClassDB::get_property_setter(p_class, entry.name) });
	}
	List<MethodInfo> signals;
	ClassDB::get_signal_list(p_class, &signals, true);
	for (const MethodInfo &entry : signals) {
		descriptor.signals.insert(entry.name, entry);
	}
	List<StringName> enums;
	ClassDB::get_enum_list(p_class, &enums, true);
	for (const StringName &name : enums) {
		List<StringName> constants;
		ClassDB::get_enum_constants(p_class, name, &constants, true);
		for (const StringName &constant : constants) {
			const int64_t value = ClassDB::get_integer_constant(p_class, constant);
			descriptor.enums[name].insert(constant, value);
			descriptor.constants.insert(constant, value);
		}
	}
}

void WGodotNativeInterfaces::Builder::inherits(const StringName &p_interface) {
	const Descriptor *parent = get_descriptor(p_interface);
	ERR_FAIL_NULL_MSG(parent, "Register parent interfaces before their children.");
	ERR_FAIL_COND_MSG(!ClassDB::is_parent_class(descriptor.native_base, parent->native_base), "The interface requires an incompatible native base.");
	descriptor.parents.append_array(parent->parents);
	descriptor.parents.push_back(p_interface);
	for (const auto &entry : parent->methods) {
		descriptor.methods.insert(entry.key, entry.value);
	}
	for (const auto &entry : parent->properties) {
		descriptor.properties.insert(entry.key, entry.value);
	}
	for (const auto &entry : parent->signals) {
		descriptor.signals.insert(entry.key, entry.value);
	}
	for (const auto &entry : parent->enums) {
		descriptor.enums.insert(entry.key, entry.value);
	}
	for (const auto &entry : parent->constants) {
		descriptor.constants.insert(entry.key, entry.value);
	}
}

void WGodotNativeInterfaces::register_descriptor(const Descriptor &p_descriptor) {
	ERR_FAIL_COND_MSG(descriptors().has(p_descriptor.name) || ClassDB::class_exists(p_descriptor.name), "Native interface name is already registered.");
	descriptors().insert(p_descriptor.name, p_descriptor);
	add_contract(p_descriptor.name, p_descriptor.native_base, p_descriptor.parents);
}

const WGodotNativeInterfaces::Descriptor *WGodotNativeInterfaces::get_descriptor(const StringName &p_name) {
	return descriptors().getptr(p_name);
}

void WGodotNativeInterfaces::get_descriptor_list(LocalVector<StringName> &r_names) {
	for (const auto &entry : descriptors()) {
		r_names.push_back(entry.key);
	}
	r_names.sort();
}

void WGodotNativeInterfaces::clear_descriptors() {
	descriptors().clear();
}
