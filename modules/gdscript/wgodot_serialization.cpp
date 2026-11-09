// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_gd/interface_helpers.h"

void GDScriptAnalyzer::wgodot_resolve_serialization_fields(GDScriptParser::ClassNode *p_class) {
	using P = GDScriptParser;
	if (!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "JsonSerializable") &&
			!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "BinarySerializable") &&
			!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "ProtobufSerializable")) {
		return;
	}
	const auto resolve = [&](const auto &self, const P::DataType &p_type, const P::Node *p_source) -> void {
		if (p_type.kind == P::DataType::CLASS) {
			resolve_class_interface(p_type.class_type, p_source);
		}
		for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
			self(self, p_type.get_container_element_type(i), p_source);
		}
	};
	for (const auto &member : p_class->members) {
		if (member.type == P::ClassNode::Member::VARIABLE && !member.variable->is_static) {
			resolve(resolve, member.variable->type_constraint, member.variable);
		}
	}
}
