// wgodot-changes::file
/**************************************************************************/
/*  export_builtin_aliases.cpp                                            */
/**************************************************************************/

#include "export_transform_internal.h"

#include "../gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/templates/list.h"
#include "core/variant/variant.h"

namespace WGodotGDScriptExportTransform {

void collect_builtin_class_aliases_from_type(ExportContext *p_context, const GDScriptParser::TypeNode *p_type);
void collect_builtin_class_aliases_from_pattern(ExportContext *p_context, const GDScriptParser::PatternNode *p_pattern);
void collect_builtin_class_aliases_from_expression(ExportContext *p_context, const GDScriptParser::ExpressionNode *p_expression);

bool is_supported_builtin_class_alias_target(const StringName &p_name) {
	if (p_name.is_empty()) {
		return false;
	}

	if (ClassDB::class_exists(p_name) && ClassDB::is_class_exposed(p_name)) {
		return true;
	}

	return GDScriptParser::get_builtin_type(p_name) < Variant::VARIANT_MAX;
}

void get_or_create_builtin_class_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_class_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_class_alias(p_name);
}

bool is_supported_builtin_function_alias_target(const StringName &p_name) {
	return !p_name.is_empty() && (Variant::has_utility_function(p_name) || GDScriptUtilityFunctions::function_exists(p_name));
}

StringName get_builtin_alias_owner_from_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_hard_type() || p_type.is_variant()) {
		return StringName();
	}

	if (p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type < Variant::VARIANT_MAX) {
		const StringName type_name = Variant::get_type_name(p_type.builtin_type);
		return type_name == SNAME("Variant") ? StringName() : type_name;
	}

	if (!p_type.native_type.is_empty() && ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type)) {
		return p_type.native_type;
	}

	return StringName();
}

bool is_supported_builtin_member_alias_target(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return false;
	}

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_owner);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (p_property) {
			if (p_static) {
				return Variant::has_constant(builtin_type, p_name) || Variant::has_enum(builtin_type, p_name) || Variant::get_enum_for_enumeration(builtin_type, p_name) != StringName();
			}

			Callable::CallError err;
			Variant dummy;
			Variant::construct(builtin_type, dummy, nullptr, 0, err);
			if (err.error != Callable::CallError::CALL_OK) {
				return false;
			}

			List<PropertyInfo> properties;
			dummy.get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.name == p_name) {
					return true;
				}
			}
			return false;
		}

		if (!Variant::has_builtin_method(builtin_type, p_name)) {
			return false;
		}

		const MethodInfo method_info = Variant::get_builtin_method_info(builtin_type, p_name);
		return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0);
	}

	if (!ClassDB::class_exists(p_owner) || !ClassDB::is_class_exposed(p_owner)) {
		return false;
	}

	if (p_property) {
		if (ClassDB::has_property(p_owner, p_name)) {
			return true;
		}
		if (p_static) {
			if (ClassDB::has_enum(p_owner, p_name)) {
				return true;
			}
			bool valid = false;
			(void)ClassDB::get_integer_constant(p_owner, p_name, &valid);
			return valid;
		}
		return false;
	}

	MethodInfo method_info;
	if (!ClassDB::get_method_info(p_owner, p_name, &method_info)) {
		return false;
	}

	return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0 || Engine::get_singleton()->has_singleton(p_owner));
}

void get_or_create_builtin_function_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_function_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_function_alias(p_name);
}

void add_builtin_function_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::IDENTIFIER) {
		return;
	}

	const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(p_call->callee);
	if (callee->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_function_alias(p_call->function_name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, callee, String(*alias));
}

void get_or_create_builtin_member_alias(ExportContext *p_context, const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_context == nullptr || !is_supported_builtin_member_alias_target(p_owner, p_name, p_static, p_property)) {
		return;
	}

	(void)p_context->get_or_create_builtin_member_alias(p_owner, p_name, p_static, p_property);
}

void add_builtin_method_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->function_name == SNAME("new") ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::SUBSCRIPT) {
		return;
	}

	const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute || subscript->base == nullptr || subscript->attribute == nullptr) {
		return;
	}

	const GDScriptParser::DataType base_type = subscript->base->get_datatype();
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_call->function_name, is_static, false);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, subscript->attribute, String(*alias));
}

void add_builtin_property_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_base == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType base_type = p_base->get_datatype();
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_identifier->name, is_static, true);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

void add_builtin_class_alias_name_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_identifier == nullptr || !is_supported_builtin_class_alias_target(p_identifier->name)) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_class_alias(p_identifier->name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

void collect_builtin_class_aliases_from_type(ExportContext *p_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	if (!p_type->type_chain.is_empty() && p_type->type_chain[0] != nullptr) {
		get_or_create_builtin_class_alias(p_context, p_type->type_chain[0]->name);
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		collect_builtin_class_aliases_from_type(p_context, container_type);
	}
}

void collect_builtin_class_aliases_from_expression(ExportContext *p_context, const GDScriptParser::ExpressionNode *p_expression);
void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node);

void collect_builtin_class_aliases_from_identifier(ExportContext *p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_context == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType datatype = p_identifier->get_datatype();
	if (p_identifier->source == GDScriptParser::IdentifierNode::NATIVE_CLASS) {
		get_or_create_builtin_class_alias(p_context, !datatype.native_type.is_empty() ? datatype.native_type : p_identifier->name);
	} else if (p_identifier->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE && datatype.is_meta_type) {
		if (datatype.kind == GDScriptParser::DataType::BUILTIN) {
			get_or_create_builtin_class_alias(p_context, Variant::get_type_name(datatype.builtin_type));
		} else if (datatype.kind == GDScriptParser::DataType::NATIVE) {
			get_or_create_builtin_class_alias(p_context, datatype.native_type);
		}
	}
}

void collect_builtin_class_aliases_from_pattern(ExportContext *p_context, const GDScriptParser::PatternNode *p_pattern) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			collect_builtin_class_aliases_from_expression(p_context, p_pattern->expression);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			for (const GDScriptParser::PatternNode *sub_pattern : p_pattern->array) {
				collect_builtin_class_aliases_from_pattern(p_context, sub_pattern);
			}
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				collect_builtin_class_aliases_from_expression(p_context, pair.key);
				collect_builtin_class_aliases_from_pattern(p_context, pair.value_pattern);
			}
			break;
		default:
			break;
	}
}

void collect_builtin_class_aliases_from_expression(ExportContext *p_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::IDENTIFIER:
			collect_builtin_class_aliases_from_identifier(p_context, static_cast<const GDScriptParser::IdentifierNode *>(p_expression));
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->attribute != nullptr) {
				const GDScriptParser::DataType base_type = subscript->base->get_datatype();
				const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
				if (!owner.is_empty()) {
					get_or_create_builtin_member_alias(p_context, owner, subscript->attribute->name, base_type.is_meta_type, true);
				}
			}
			collect_builtin_class_aliases_from_expression(p_context, subscript->base);
			if (!subscript->is_attribute) {
				collect_builtin_class_aliases_from_expression(p_context, subscript->index);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_builtin_class_aliases_from_expression(p_context, element);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, assignment->assignee);
			collect_builtin_class_aliases_from_expression(p_context, assignment->assigned_value);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, binary->left_operand);
			collect_builtin_class_aliases_from_expression(p_context, binary->right_operand);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			if (!call->is_super && call->callee != nullptr && call->callee->type == GDScriptParser::Node::IDENTIFIER) {
				const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(call->callee);
				if (callee->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
					get_or_create_builtin_function_alias(p_context, call->function_name);
				}
			} else if (!call->is_super && call->function_name != SNAME("new") && call->callee != nullptr && call->callee->type == GDScriptParser::Node::SUBSCRIPT) {
				const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(call->callee);
				if (subscript->is_attribute && subscript->base != nullptr && subscript->attribute != nullptr) {
					const GDScriptParser::DataType base_type = subscript->base->get_datatype();
					const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
					if (!owner.is_empty()) {
						get_or_create_builtin_member_alias(p_context, owner, call->function_name, base_type.is_meta_type, false);
					}
				}
			}
			collect_builtin_class_aliases_from_expression(p_context, call->callee);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_builtin_class_aliases_from_expression(p_context, argument);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, cast->operand);
			collect_builtin_class_aliases_from_type(p_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_builtin_class_aliases_from_expression(p_context, pair.key);
				collect_builtin_class_aliases_from_expression(p_context, pair.value);
			}
		} break;
		case GDScriptParser::Node::LAMBDA:
			collect_builtin_class_aliases_from_node(p_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function);
			break;
		case GDScriptParser::Node::PRELOAD:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path);
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, ternary->condition);
			collect_builtin_class_aliases_from_expression(p_context, ternary->true_expr);
			collect_builtin_class_aliases_from_expression(p_context, ternary->false_expr);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			collect_builtin_class_aliases_from_expression(p_context, type_test->operand);
			collect_builtin_class_aliases_from_type(p_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
			break;
		default:
			break;
	}
}

void collect_builtin_class_aliases_from_annotations(ExportContext *p_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		for (const GDScriptParser::ExpressionNode *argument : annotation->arguments) {
			collect_builtin_class_aliases_from_expression(p_context, argument);
		}
	}
}

void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	collect_builtin_class_aliases_from_annotations(p_context, p_node);

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS: {
			const GDScriptParser::ClassNode *class_node = static_cast<const GDScriptParser::ClassNode *>(p_node);
			for (const GDScriptParser::IdentifierNode *identifier : class_node->extends) {
				if (identifier != nullptr) {
					get_or_create_builtin_class_alias(p_context, identifier->name);
				}
			}
			for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
				collect_builtin_class_aliases_from_node(p_context, member.get_source_node());
			}
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, constant->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, constant->initializer);
		} break;
		case GDScriptParser::Node::FUNCTION: {
			const GDScriptParser::FunctionNode *function = static_cast<const GDScriptParser::FunctionNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				collect_builtin_class_aliases_from_node(p_context, parameter);
			}
			collect_builtin_class_aliases_from_node(p_context, function->rest_parameter);
			collect_builtin_class_aliases_from_type(p_context, function->return_type);
			collect_builtin_class_aliases_from_node(p_context, function->body);
		} break;
		case GDScriptParser::Node::PARAMETER: {
			const GDScriptParser::ParameterNode *parameter = static_cast<const GDScriptParser::ParameterNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, parameter->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, parameter->initializer);
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				collect_builtin_class_aliases_from_node(p_context, parameter);
			}
		} break;
		case GDScriptParser::Node::SUITE: {
			const GDScriptParser::SuiteNode *suite = static_cast<const GDScriptParser::SuiteNode *>(p_node);
			for (const GDScriptParser::Node *statement : suite->statements) {
				collect_builtin_class_aliases_from_node(p_context, statement);
			}
		} break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, variable->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, variable->initializer);
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
				collect_builtin_class_aliases_from_node(p_context, variable->setter);
				collect_builtin_class_aliases_from_node(p_context, variable->getter);
			}
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_node);
			collect_builtin_class_aliases_from_type(p_context, for_node->datatype_specifier);
			collect_builtin_class_aliases_from_expression(p_context, for_node->list);
			collect_builtin_class_aliases_from_node(p_context, for_node->loop);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, if_node->condition);
			collect_builtin_class_aliases_from_node(p_context, if_node->true_block);
			collect_builtin_class_aliases_from_node(p_context, if_node->false_block);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, match_node->test);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				collect_builtin_class_aliases_from_node(p_context, branch);
			}
		} break;
		case GDScriptParser::Node::MATCH_BRANCH: {
			const GDScriptParser::MatchBranchNode *branch = static_cast<const GDScriptParser::MatchBranchNode *>(p_node);
			for (const GDScriptParser::PatternNode *pattern : branch->patterns) {
				collect_builtin_class_aliases_from_pattern(p_context, pattern);
			}
			collect_builtin_class_aliases_from_node(p_context, branch->guard_body);
			collect_builtin_class_aliases_from_node(p_context, branch->block);
		} break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, assert_node->condition);
			collect_builtin_class_aliases_from_expression(p_context, assert_node->message);
		} break;
		case GDScriptParser::Node::RETURN:
			collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::ReturnNode *>(p_node)->return_value);
			break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_node);
			collect_builtin_class_aliases_from_expression(p_context, while_node->condition);
			collect_builtin_class_aliases_from_node(p_context, while_node->loop);
		} break;
		default:
			if (p_node->is_expression()) {
				collect_builtin_class_aliases_from_expression(p_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node));
			}
			break;
	}
}

} // namespace WGodotGDScriptExportTransform
