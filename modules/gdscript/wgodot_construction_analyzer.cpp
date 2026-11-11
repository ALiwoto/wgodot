// wgodot-changes::file
#include "gdscript_analyzer.h"

using Parser = GDScriptParser;

void GDScriptAnalyzer::wgodot_resolve_field_initializers(Parser::CallNode *p_call, const Parser::DataType &p_type) {
	if (!p_call->wgodot_has_field_initializers) {
		return;
	}
	if (p_type.kind != Parser::DataType::CLASS || !p_type.is_meta_type) {
		push_error("Field initializers require a statically known script class.", p_call);
		return;
	}
	HashSet<StringName> supplied;
	for (auto &entry : p_call->wgodot_field_initializers) {
		reduce_expression(entry.value);
		const StringName &name = entry.name->name;
		if (supplied.has(name)) {
			push_error(vformat("Field '%s' is initialized more than once.", name), entry.name);
			continue;
		}
		supplied.insert(name);
		Parser::ClassNode *owner = p_type.class_type;
		while (owner && !owner->has_member(name)) {
			owner = owner->base_type.class_type;
		}
		if (!owner || owner->get_member(name).type != Parser::ClassNode::Member::VARIABLE) {
			push_error(vformat("Unknown instance field '%s'.", name), entry.name);
			continue;
		}
		resolve_class_member(owner, name, entry.name);
		const auto member = owner->get_member(name);
		entry.field = member.variable;
		wgodot_validate_private_member_access(member, owner, entry.name);
		wgodot_validate_protected_member_access(member, owner, entry.name);
		if (entry.field->is_static || entry.field->property != Parser::VariableNode::PROP_NONE) {
			push_error("Construction initializers accept direct instance fields, not static fields or accessor properties.", entry.name);
			continue;
		}
		const auto &expected = entry.field->type_constraint;
		if (entry.value->type == Parser::Node::ARRAY && expected.has_container_element_type(0)) {
			update_array_literal_element_type(static_cast<Parser::ArrayNode *>(entry.value), expected.get_container_element_type(0));
		} else if (entry.value->type == Parser::Node::DICTIONARY && expected.has_container_element_types()) {
			update_dictionary_literal_element_type(static_cast<Parser::DictionaryNode *>(entry.value), expected.get_container_element_type_or_variant(0), expected.get_container_element_type_or_variant(1));
		}
		wgodot_validate_immutable_value(expected, entry.value);
		if (!is_type_compatible(expected, entry.value->type_constraint, true, entry.value)) {
			push_error(vformat("Cannot initialize field '%s' of type '%s' with '%s'.", name, expected.to_string(), entry.value->type_constraint.to_string()), entry.value);
		}
	}
}
