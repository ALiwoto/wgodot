// wgodot-changes::file
#include "wgodot_resource_schema.h"

#include "wgodot_resource_identity.h"

namespace {
String schema_type(const GDScriptParser::DataType &p_type, HashSet<const GDScriptParser::ClassNode *> &r_records) {
	String signature = p_type.to_string() + ":" + itos(p_type.wgodot_resource_path) + ":" + itos(p_type.wgodot_scalar) + ":" + itos(p_type.wgodot_value);
	for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
		signature += "[" + schema_type(p_type.get_container_element_type(i), r_records) + "]";
	}
	if (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type->wgodot_record && !r_records.has(p_type.class_type)) {
		r_records.insert(p_type.class_type);
		for (const auto &member : p_type.class_type->members) {
			if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				signature += "{" + String(member.variable->identifier->name) + ":" + schema_type(member.variable->type_constraint, r_records) + "}";
			}
		}
	}
	return signature;
}
} // namespace

WGodotResourceSchema::WGodotResourceSchema(const GDScriptParser::ClassNode *p_class) {
	id = WGodotResourceIdentity::from_path(p_class->fqcn);
	for (const auto *type = p_class; type; type = type->base_type.kind == GDScriptParser::DataType::CLASS ? type->base_type.class_type : nullptr) {
		for (const auto &member : type->members) {
			if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				const auto *field = member.variable;
				if (field->exported && !field->is_static && !field->wgodot_private && !field->wgodot_protected) {
					fields.push_back(field);
				}
			}
		}
	}
	struct Order {
		bool operator()(const GDScriptParser::VariableNode *p_left, const GDScriptParser::VariableNode *p_right) const {
			return String(p_left->identifier->name) < String(p_right->identifier->name);
		}
	};
	fields.sort_custom<Order>();
	String signature = "resource-protobuf-v3:" + itos(sizeof(real_t));
	HashSet<const GDScriptParser::ClassNode *> records;
	for (const auto *field : fields) {
		signature += "\n" + String(field->identifier->name) + ":" + schema_type(field->type_constraint, records);
	}
	fingerprint = WGodotResourceIdentity::from_path(signature);
}

uint32_t WGodotResourceSchema::field_id(const StringName &p_name) const {
	for (int i = 0; i < fields.size(); i++) {
		if (fields[i]->identifier->name == p_name) {
			return i + 1;
		}
	}
	return 0;
}
