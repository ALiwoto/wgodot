// wgodot-changes::file

#include "interface_helpers.h"

#include "../gdscript.h"

#include "core/object/class_db.h"
#include "core/object/wgodot_native_interfaces.h"

namespace WGodotGDScriptInterfaceHelpers {

bool is_contract_member(const GDScriptParser::ClassNode::Member &p_member) {
	using Member = GDScriptParser::ClassNode::Member;
	return p_member.type == Member::FUNCTION || p_member.type == Member::VARIABLE || p_member.type == Member::SIGNAL;
}

bool is_implemented_member(const GDScriptParser::ClassNode::Member &p_member) {
	using Member = GDScriptParser::ClassNode::Member;
	switch (p_member.type) {
		case Member::FUNCTION:
			return p_member.function->wgodot_interface_implementation;
		case Member::VARIABLE:
			return p_member.variable->wgodot_interface_implementation;
		case Member::SIGNAL:
			return p_member.signal->wgodot_interface_implementation;
		default:
			return false;
	}
}

bool member_has_no_mangle(const GDScriptParser::ClassNode::Member &p_member) {
	using Member = GDScriptParser::ClassNode::Member;
	switch (p_member.type) {
		case Member::FUNCTION:
			return p_member.function->wgodot_no_mangle;
		case Member::VARIABLE:
			return p_member.variable->wgodot_no_mangle;
		case Member::SIGNAL:
			return p_member.signal->wgodot_no_mangle;
		default:
			return false;
	}
}

bool same_type(const GDScriptParser::DataType &p_first, const GDScriptParser::DataType &p_second) {
	using DataType = GDScriptParser::DataType;
	if (p_first.has_no_type() || p_second.has_no_type()) {
		return p_first.has_no_type() && p_second.has_no_type();
	}
	if (p_first.kind != p_second.kind || p_first.is_meta_type != p_second.is_meta_type || p_first.container_element_types.size() != p_second.container_element_types.size()) {
		return false;
	}
	for (int i = 0; i < p_first.container_element_types.size(); i++) {
		if (!same_type(p_first.get_container_element_type(i), p_second.get_container_element_type(i))) {
			return false;
		}
	}
	switch (p_first.kind) {
		case DataType::CLASS:
			return get_interface_id(p_first.class_type) == get_interface_id(p_second.class_type);
		case DataType::SCRIPT:
			return p_first.script_type == p_second.script_type;
		case DataType::NATIVE:
		case DataType::ENUM:
			return p_first.native_type == p_second.native_type;
		case DataType::BUILTIN:
			return p_first.builtin_type == p_second.builtin_type;
		default:
			return p_first.kind == p_second.kind;
	}
}

String get_interface_id(const GDScriptParser::ClassNode *p_interface) {
	return GDScript::canonicalize_path(p_interface->self_type.script_path) + "::" + p_interface->fqcn;
}

const WGodotNativeInterfaces::Descriptor *native_interface_for_member(const GDScriptParser::DataType &p_type, const StringName &p_member) {
	if (p_type.kind == GDScriptParser::DataType::NATIVE) {
		return WGodotNativeInterfaces::get_descriptor(p_type.native_type);
	}
	if (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type->wgodot_is_interface) {
		for (const StringName &name : p_type.class_type->wgodot_native_interfaces) {
			const auto *type = WGodotNativeInterfaces::get_descriptor(name);
			if (type && (type->methods.has(p_member) || type->properties.has(p_member) || type->signals.has(p_member) || type->enums.has(p_member) || type->constants.has(p_member))) {
				return type;
			}
		}
	}
	return nullptr;
}

bool class_implements_native_interface(const GDScriptParser::ClassNode *p_class, const StringName &p_interface) {
	for (const auto *type = p_class; type; type = type->base_type.class_type) {
		if (type->wgodot_native_interfaces.has(p_interface)) {
			return true;
		}
		if (type->base_type.script_type.is_valid() && type->base_type.script_type->wgodot_implements_interface(p_interface)) {
			return true;
		}
		if (WGodotNativeInterfaces::accepts(type->base_type.native_type, p_interface)) {
			return true;
		}
	}
	return false;
}

bool class_implements_interface_type(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_interface) {
	if (p_class == nullptr || p_interface == nullptr || !p_interface->wgodot_is_interface) {
		return false;
	}
	const String id = get_interface_id(p_interface);
	for (const GDScriptParser::ClassNode *current = p_class; current != nullptr;) {
		if (current->wgodot_is_interface && get_interface_id(current) == id) {
			return true;
		}
		for (const GDScriptParser::ClassNode *implemented : current->wgodot_resolved_interfaces) {
			if (get_interface_id(implemented) == id) {
				return true;
			}
		}
		if (current->base_type.kind == GDScriptParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else {
			if (current->base_type.script_type.is_valid() && current->base_type.script_type->wgodot_implements_interface(id)) {
				return true;
			}
			return ClassDB::wgodot_class_implements_interface(current->base_type.native_type, id);
		}
	}
	return false;
}

} // namespace WGodotGDScriptInterfaceHelpers
