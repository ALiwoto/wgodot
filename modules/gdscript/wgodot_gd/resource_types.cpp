// wgodot-changes::file
#include "core/io/wgodot_resource_value.h"

#include "modules/gdscript/gdscript.h"

namespace {
WGodotResourceDataType declared_type(const GDScriptDataType &p_type) {
	WGodotResourceDataType result;
	if (!p_type.has_type()) {
		return result;
	}
	result.builtin = p_type.builtin_type;
	result.value_container = p_type.wgodot_value;
	if (result.builtin == Variant::WRECORD || p_type.wgodot_class_ref) {
		result.name = p_type.native_type;
	}
	if (p_type.kind != GDScriptDataType::BUILTIN) {
		result.builtin = Variant::OBJECT;
		result.name = p_type.script_type ? p_type.script_type->get_global_name() : p_type.native_type;
	}
	for (const GDScriptDataType &element : p_type.container_element_types) {
		result.elements.push_back(declared_type(element));
	}
	return result;
}
} // namespace

bool GDScript::wgodot_get_declared_property_type(const StringName &p_name, WGodotResourceDataType &r_type) const {
	const MemberInfo *member = member_indices.getptr(p_name);
	if (!member) {
		return false;
	}
	r_type = declared_type(member->data_type);
	return true;
}
