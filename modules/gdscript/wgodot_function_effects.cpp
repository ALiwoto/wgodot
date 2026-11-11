// wgodot-changes::file
#include "gdscript_analyzer.h"

#include "gdscript.h"
#include "gdscript_utility_functions.h"
#include "wgodot_gd/ast.h"

#include <functional>

namespace {
using Parser = GDScriptParser;

class EffectVisitor : public WGodotGDScriptAstVisitor {
	const std::function<void(const Parser::Node *)> &callback;

	bool visit(const Parser::Node *p_node) override {
		callback(p_node);
		return true;
	}

	bool descend(const Parser::Node *p_node) override {
		return p_node->type != Parser::Node::LAMBDA;
	}

public:
	explicit EffectVisitor(const std::function<void(const Parser::Node *)> &p_callback) : callback(p_callback) {}
};

bool is_self(const Parser::ExpressionNode *p_expression) {
	return p_expression && p_expression->type == Parser::Node::SELF;
}

const Parser::VariableNode *local_variable(const Parser::ExpressionNode *p_expression) {
	if (!p_expression || p_expression->type != Parser::Node::IDENTIFIER) {
		return nullptr;
	}
	const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
	return identifier->source == Parser::IdentifierNode::LOCAL_VARIABLE ? identifier->variable_source : nullptr;
}

bool initializes_data(const Parser::ClassNode *p_class, const Parser::FunctionNode *p_function) {
	if (!p_class->wgodot_data_class || p_function->is_static) {
		return false;
	}
	if (p_function->identifier && p_function->identifier->name == GDScriptLanguage::get_singleton()->strings._init) {
		return true;
	}
	// Property assignment during B or D can dispatch a setter, including an
	// override of an inherited named setter. These bodies must not publish self.
	for (const auto *owner = p_class; owner; owner = owner->base_type.class_type) {
		for (const auto &member : owner->members) {
			if (member.type != Parser::ClassNode::Member::VARIABLE || member.variable->is_static) {
				continue;
			}
			const auto *field = member.variable;
			if (field->property == Parser::VariableNode::PROP_INLINE && field->setter == p_function) {
				return true;
			}
			if (field->property == Parser::VariableNode::PROP_SETGET && field->setter_pointer && p_function->identifier && field->setter_pointer->name == p_function->identifier->name) {
				return true;
			}
		}
	}
	return false;
}
} // namespace

void GDScriptAnalyzer::wgodot_validate_initializer_effects(Parser::VariableNode *p_variable) {
	if (!parser->current_class->wgodot_data_class || p_variable->is_static || !p_variable->initializer) {
		return;
	}
	Parser::IdentifierNode identifier;
	identifier.name = GDScriptLanguage::get_singleton()->strings._init;
	Parser::SuiteNode body;
	body.statements.push_back(p_variable->initializer);
	Parser::FunctionNode initializer;
	initializer.identifier = &identifier;
	initializer.body = &body;
	wgodot_validate_function_effects(&initializer);
}

void GDScriptAnalyzer::wgodot_validate_function_effects(Parser::FunctionNode *p_function) {
	const bool initializing = initializes_data(parser->current_class, p_function);
	const bool protect_self = initializing || p_function->wgodot_readonly;
	const bool clone = p_function->wgodot_immutable_clone;
	if (!protect_self && !clone) {
		return;
	}

	HashSet<const Parser::Node *> receiver_uses;
	HashSet<const Parser::VariableNode *> clone_locals;
	HashSet<const Parser::VariableNode *> result_aliases;
	const std::function<bool(const Parser::ExpressionNode *)> is_result_type = [&](const Parser::ExpressionNode *p_expression) {
		if (!p_expression) {
			return false;
		}
		if (p_expression->type == Parser::Node::CAST) {
			return is_result_type(static_cast<const Parser::CastNode *>(p_expression)->operand);
		}
		if (p_expression->type == Parser::Node::TERNARY_OPERATOR) {
			const auto *branches = static_cast<const Parser::TernaryOpNode *>(p_expression);
			return is_result_type(branches->true_expr) || is_result_type(branches->false_expr);
		}
		return result_aliases.has(local_variable(p_expression)) ||
				(p_expression->type_constraint.kind == Parser::DataType::CLASS &&
						p_expression->type_constraint.class_type == parser->current_class && !p_expression->type_constraint.is_meta_type);
	};
	const auto check_publication = [&](const Parser::ExpressionNode *p_expression) {
		if (clone && is_result_type(p_expression)) {
			push_error("An @immutable_clone method cannot publish an instance of its result class. Use @immutable_return for shared results.", p_expression);
		}
	};

	// Collect candidate results first, so returns in any control-flow branch are
	// checked against the same declaration and all later assignments are checked.
	const std::function<void(const Parser::Node *)> collect = [&](const Parser::Node *p_node) {
		if (clone && p_node->type == Parser::Node::VARIABLE) {
			const auto *variable = static_cast<const Parser::VariableNode *>(p_node);
			if (variable->type_constraint.class_type == parser->current_class && variable->type_constraint.wgodot_immutable &&
					variable->initializer && variable->initializer->wgodot_fresh_value) {
				clone_locals.insert(variable);
			}
		}
	};
	EffectVisitor collector(collect);
	collector.walk(p_function->body);
	if (clone) {
		uint32_t previous_size;
		const std::function<void(const Parser::Node *)> collect_aliases = [&](const Parser::Node *p_node) {
			if (p_node->type == Parser::Node::VARIABLE) {
				const auto *variable = static_cast<const Parser::VariableNode *>(p_node);
				if (is_result_type(variable->initializer)) {
					result_aliases.insert(variable);
				}
			} else if (p_node->type == Parser::Node::ASSIGNMENT) {
				const auto *assignment = static_cast<const Parser::AssignmentNode *>(p_node);
				const auto *variable = local_variable(assignment->assignee);
				if (variable && is_result_type(assignment->assigned_value)) {
					result_aliases.insert(variable);
				}
			}
		};
		EffectVisitor aliases(collect_aliases);
		do {
			previous_size = result_aliases.size();
			aliases.walk(p_function->body);
		} while (previous_size != result_aliases.size());
	}

	const std::function<void(const Parser::Node *)> check = [&](const Parser::Node *p_node) {
		switch (p_node->type) {
			case Parser::Node::CALL: {
				const auto *call = static_cast<const Parser::CallNode *>(p_node);
				receiver_uses.insert(call->callee);
				bool self_call = call->is_super;
				if (call->get_callee_type() == Parser::Node::SUBSCRIPT) {
					self_call |= is_self(static_cast<const Parser::SubscriptNode *>(call->callee)->base);
				} else if (call->get_callee_type() == Parser::Node::IDENTIFIER) {
					self_call |= call->wgodot_resolved_function && !call->wgodot_resolved_function->is_static;
					self_call |= call->wgodot_native_call && !GDScriptUtilityFunctions::function_exists(call->function_name) && !Variant::has_utility_function(call->function_name);
				}
				const bool parent_constructor = call->is_super && call->function_name == GDScriptLanguage::get_singleton()->strings._init;
				if (protect_self && parser->current_class->wgodot_data_class && self_call && call->wgodot_native_call && !call->is_static && !parent_constructor) {
					push_error("Native self calls are not verified as non-publishing in data-class initialization, setters or readonly helpers.", call);
				}
				if (initializing && self_call && !call->wgodot_readonly_call && !parent_constructor) {
					push_error("Data-class initialization may only call readonly, non-publishing self helpers.", call);
				}
				for (const auto *argument : call->arguments) {
					check_publication(argument);
				}
				for (const auto &entry : call->wgodot_field_initializers) {
					check_publication(entry.value);
				}
			} break;
			case Parser::Node::SUBSCRIPT: {
				const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_node);
				if (is_self(subscript->base)) {
					receiver_uses.insert(subscript->base);
				}
				if (protect_self && is_self(subscript->base) && subscript->type_constraint.builtin_type == Variant::CALLABLE && !receiver_uses.has(subscript)) {
					push_error("Cannot publish a callable bound to the protected self receiver.", subscript);
				}
			} break;
			case Parser::Node::SELF:
				if (protect_self && !receiver_uses.has(p_node)) {
					push_error("Data-class initialization and setters, and @readonly methods, cannot publish or alias self; access fields or call readonly helpers directly.", p_node);
				}
				break;
			case Parser::Node::IDENTIFIER: {
				const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_node);
				if (protect_self && identifier->source == Parser::IdentifierNode::MEMBER_FUNCTION && !identifier->function_source_is_static && !receiver_uses.has(identifier)) {
					push_error("Cannot publish a callable bound to the protected self receiver.", identifier);
				}
			} break;
			case Parser::Node::ASSIGNMENT: {
				const auto *assignment = static_cast<const Parser::AssignmentNode *>(p_node);
				if (initializing) {
					const Parser::ExpressionNode *target = assignment->assignee;
					while (target->type == Parser::Node::SUBSCRIPT) {
						const auto *access = static_cast<const Parser::SubscriptNode *>(target);
						target = access->is_attribute && is_self(access->base) ? access->attribute : access->base;
					}
					if (target->type == Parser::Node::IDENTIFIER && static_cast<const Parser::IdentifierNode *>(target)->source == Parser::IdentifierNode::INHERITED_VARIABLE) {
						push_error("Data-class initialization cannot invoke an unaudited native property setter; initialize direct script fields instead.", assignment);
					}
				}
				const auto *local = local_variable(assignment->assignee);
				if (clone && local && clone_locals.has(local)) {
					if (!assignment->assigned_value->wgodot_fresh_value) {
						push_error("A clone result local may only be reassigned from fresh construction or a verified clone.", assignment);
					}
				} else if (!local) {
					check_publication(assignment->assigned_value);
				}
			} break;
			case Parser::Node::ARRAY:
				for (const auto *element : static_cast<const Parser::ArrayNode *>(p_node)->elements) {
					check_publication(element);
				}
				break;
			case Parser::Node::DICTIONARY:
				for (const auto &element : static_cast<const Parser::DictionaryNode *>(p_node)->elements) {
					check_publication(element.key);
					check_publication(element.value);
				}
				break;
			case Parser::Node::LAMBDA: {
				const auto *lambda = static_cast<const Parser::LambdaNode *>(p_node);
				if (protect_self && lambda->use_self) {
					push_error("Cannot capture self in data-class initialization, setters or @readonly methods.", lambda);
				}
				for (const auto *capture : lambda->captures) {
					check_publication(capture);
				}
			} break;
			case Parser::Node::RETURN: {
				const auto *value = static_cast<const Parser::ReturnNode *>(p_node)->return_value;
				if (clone && (!value || (!value->wgodot_fresh_value && !clone_locals.has(local_variable(value))))) {
					push_error("@immutable_clone must return fresh construction, a verified clone, or an @immutable local initialized exclusively from those expressions.", p_node);
				}
			} break;
			default:
				break;
		}
	};
	EffectVisitor checker(check);
	checker.walk(p_function->body);
}
