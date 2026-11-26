// wgodot-changes::file
#include "wgodot_resource_encode.h"

namespace WGodotResourceEncode {
Type::Type(const GDScriptParser::DataType &p_type) {
	using Parsed = GDScriptParser::DataType;
	builtin_type = p_type.builtin_type;
	float32 = p_type.wgodot_scalar == WGodotScalar::FLOAT32;
	wgodot_resource_path = p_type.wgodot_resource_path;
	wgodot_class_ref = p_type.wgodot_class_ref;
	value_container = p_type.wgodot_value;
	if (p_type.is_variant() || !p_type.is_hard_type()) {
		builtin_type = Variant::NIL;
	} else if (p_type.kind == Parsed::ENUM) {
		builtin_type = Variant::INT;
	} else if (p_type.kind == Parsed::CLASS) {
		name = p_type.class_type->fqcn;
		builtin_type = p_type.class_type->wgodot_record ? Variant::WRECORD : Variant::OBJECT;
	} else if (p_type.kind == Parsed::NATIVE) {
		name = p_type.native_type;
		builtin_type = Variant::OBJECT;
	}
	for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
		elements.push_back(Type(p_type.get_container_element_type(i)));
	}
	if (builtin_type == Variant::WRECORD) {
		for (const auto &member : p_type.class_type->members) {
			if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				members.push_back(member.variable->identifier->name);
				elements.push_back(Type(member.variable->type_constraint));
			}
		}
	}
}

Type::Type(const Dictionary &p_data) {
	builtin_type = Variant::Type(int(p_data["builtin"]));
	name = p_data["name"];
	float32 = p_data["float32"];
	wgodot_resource_path = p_data["path"];
	wgodot_class_ref = p_data["class_ref"];
	value_container = p_data["value"];
	const Array children = p_data["elements"];
	for (const Variant &child : children) {
		elements.push_back(Type(Dictionary(child)));
	}
	const PackedStringArray names = p_data["members"];
	for (const String &member : names) {
		members.push_back(member);
	}
}

Dictionary Type::serialize() const {
	Dictionary result;
	result["builtin"] = int(builtin_type);
	result["name"] = name;
	result["float32"] = float32;
	result["path"] = wgodot_resource_path;
	result["class_ref"] = wgodot_class_ref;
	result["value"] = value_container;
	Array children;
	for (const Type &element : elements) {
		children.push_back(element.serialize());
	}
	result["elements"] = children;
	PackedStringArray names;
	for (const StringName &member : members) {
		names.push_back(member);
	}
	result["members"] = names;
	return result;
}

WGodotResourceDataType Type::declaration() const {
	WGodotResourceDataType result;
	result.builtin = builtin_type;
	// The document reader uses non-executing Resource placeholders. Validate
	// their nominal types against the frozen schema after parsing each value.
	result.name = builtin_type == Variant::OBJECT ? SNAME("Resource") : name;
	result.resource_path = wgodot_resource_path;
	result.value_container = value_container;
	for (const Type &element : elements) {
		result.elements.push_back(element.declaration());
	}
	return result;
}
} // namespace WGodotResourceEncode
