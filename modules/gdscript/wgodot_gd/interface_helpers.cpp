// wgodot-changes::file
/**************************************************************************/
/*  interface_helpers.cpp                                                 */
/**************************************************************************/

#include "interface_helpers.h"

namespace {

StringName get_interface_type_name(const GDScriptParser::ClassNode *p_interface) {
	if (p_interface == nullptr) {
		return StringName();
	}
	if (p_interface->wgodot_interface_name != StringName()) {
		return p_interface->wgodot_interface_name;
	}
	if (p_interface->identifier != nullptr) {
		return p_interface->identifier->name;
	}
	return StringName();
}

bool interface_reference_matches_type(const GDScriptParser::ClassNode::WGodotInterfaceReference &p_reference, const GDScriptParser::ClassNode *p_interface) {
	if (p_interface == nullptr || !p_interface->wgodot_is_interface || p_reference.identifiers.is_empty()) {
		return false;
	}

	return p_reference.identifiers[0]->name == get_interface_type_name(p_interface);
}

} // namespace

namespace WGodotGDScriptInterfaceHelpers {

bool class_implements_interface_type(const GDScriptParser::ClassNode *p_class, const GDScriptParser::ClassNode *p_interface) {
	if (p_class == nullptr || p_interface == nullptr || !p_interface->wgodot_is_interface) {
		return false;
	}

	for (const GDScriptParser::ClassNode *current_class = p_class; current_class != nullptr;) {
		if (current_class == p_interface || current_class->fqcn == p_interface->fqcn) {
			return true;
		}
		for (const GDScriptParser::ClassNode::WGodotInterfaceReference &interface_reference : current_class->wgodot_implements) {
			if (interface_reference_matches_type(interface_reference, p_interface)) {
				return true;
			}
		}

		current_class = current_class->base_type.kind == GDScriptParser::DataType::CLASS ? current_class->base_type.class_type : nullptr;
	}

	return false;
}

} // namespace WGodotGDScriptInterfaceHelpers
