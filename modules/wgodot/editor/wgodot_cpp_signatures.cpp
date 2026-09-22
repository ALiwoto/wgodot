// wgodot-changes::file
#include "wgodot_cpp_signatures.h"

#include "wgodot_cpp_array_api.h"
#include "wgodot_cpp_ast.h"

#include "core/object/class_db.h"

#include "modules/gdscript/gdscript_analyzer.h"

#include <functional>

using Parser = GDScriptParser;

namespace {
Parser::DataType builtin(Variant::Type p_type) {
	Parser::DataType type;
	type.kind = Parser::DataType::BUILTIN;
	type.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_type;
	return type;
}

const Parser::ExpressionNode *signal_emit_source(const Parser::Node *p_node) {
	if (p_node->type != Parser::Node::SUBSCRIPT) {
		return nullptr;
	}
	const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_node);
	const auto &base_type = subscript->base->type_constraint;
	if (subscript->is_attribute && subscript->attribute->name == SNAME("emit") &&
			base_type.kind == Parser::DataType::BUILTIN && base_type.builtin_type == Variant::SIGNAL && !base_type.is_meta_type) {
		return subscript->base;
	}
	return nullptr;
}

class Collector : public WGodotCppAstVisitor {
	std::function<void(const Parser::Node *)> callback;
	const Parser::Node *root;
	bool visit(const Parser::Node *p_node) override {
		callback(p_node);
		return true;
	}
	bool descend(const Parser::Node *p_node) override {
		return p_node == root || (p_node->type != Parser::Node::FUNCTION && p_node->type != Parser::Node::CLASS);
	}

public:
	Collector(const Parser::Node *p_root, std::function<void(const Parser::Node *)> p_callback) : callback(std::move(p_callback)), root(p_root) { walk(root); }
};
} // namespace

bool WGodotCppSignatures::contains_signature(const Parser::DataType &p_type) {
	if (p_type.kind != Parser::DataType::BUILTIN) {
		return false;
	}
	if (p_type.builtin_type == Variant::CALLABLE || p_type.builtin_type == Variant::SIGNAL) {
		return true;
	}
	return p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0) && contains_signature(p_type.get_container_element_type(0));
}

const Parser::Node *WGodotCppSignatures::member(const Parser::DataType &p_type, const StringName &p_name) const {
	if (p_type.kind != Parser::DataType::CLASS) {
		return nullptr;
	}
	if (p_type.class_type->has_member(p_name)) {
		return p_type.class_type->get_member(p_name).get_source_node();
	}
	return member(p_type.class_type->base_type, p_name);
}

const Parser::Node *WGodotCppSignatures::source(const Parser::Node *p_node) const {
	if (!p_node) {
		return nullptr;
	}
	if (p_node->type == Parser::Node::IDENTIFIER) {
		const auto *id = static_cast<const Parser::IdentifierNode *>(p_node);
		switch (id->source) {
			case Parser::IdentifierNode::FUNCTION_PARAMETER:
				return id->parameter_source;
			case Parser::IdentifierNode::LOCAL_VARIABLE:
			case Parser::IdentifierNode::MEMBER_VARIABLE:
			case Parser::IdentifierNode::STATIC_VARIABLE:
				return id->variable_source;
			case Parser::IdentifierNode::LOCAL_ITERATOR:
			case Parser::IdentifierNode::LOCAL_BIND:
				return id->bind_source;
			case Parser::IdentifierNode::MEMBER_SIGNAL:
				return id->signal_source;
			case Parser::IdentifierNode::MEMBER_FUNCTION:
				return id->function_source;
			case Parser::IdentifierNode::INHERITED_VARIABLE:
				return member(scopes[p_node].owner->node->self_type, id->name);
			default:
				break;
		}
	}
	if (p_node->type == Parser::Node::SUBSCRIPT) {
		const auto *sub = static_cast<const Parser::SubscriptNode *>(p_node);
		return sub->is_attribute ? member(sub->base->type_constraint, sub->attribute->name) : sub->base;
	}
	return nullptr;
}

void WGodotCppSignatures::link(const Parser::Node *p_source, const Parser::Node *p_target, bool p_reverse) {
	if (p_source && p_target && p_source != p_target) {
		links.push_back({ p_source, p_target, p_reverse });
	}
}

WGodotCppSignatures::Signature WGodotCppSignatures::function(const Parser::FunctionNode *p_function) const {
	Signature signature;
	signature.result = { p_function->return_type_constraint, p_function };
	signature.result.type.is_coroutine = p_function->is_coroutine;
	const uint32_t captures = p_function->source_lambda ? p_function->source_lambda->captures.size() : 0;
	for (uint32_t i = captures; i < p_function->parameters.size(); i++) {
		const auto *parameter = p_function->parameters[i];
		signature.arguments.push_back({ parameter->type_constraint, parameter });
		signature.defaults += parameter->initializer != nullptr;
	}
	return signature;
}

void WGodotCppSignatures::collect(const Parser::Node *p_node, Scope p_scope) {
	if (!p_node || scopes.has(p_node)) {
		return;
	}
	Collector visitor(p_node, [&](const Parser::Node *p_child) {
		if (p_child != p_node && p_child->type == Parser::Node::CLASS) {
			return;
		}
		if (p_child->type == Parser::Node::FUNCTION) {
			auto scope = p_scope;
			scope.function = static_cast<const Parser::FunctionNode *>(p_child);
			if (p_child != p_node) {
				collect(p_child, scope);
				return;
			}
			p_scope = scope;
		}
		if (p_child->type == Parser::Node::SUITE) {
			for (const auto *statement : static_cast<const Parser::SuiteNode *>(p_child)->statements) {
				if (statement->is_expression()) {
					discarded.insert(statement);
				}
			}
		}
		scopes.insert(p_child, p_scope);
		nodes.push_back(p_child);
	});
}

void WGodotCppSignatures::seed(const Parser::Node *p_node) {
	if (p_node->type == Parser::Node::SIGNAL) {
		const auto *signal = static_cast<const Parser::SignalNode *>(p_node);
		Signature signature;
		signature.signal = true;
		signature.result = { builtin(Variant::NIL), signal };
		for (const auto *parameter : signal->parameters) {
			signature.arguments.push_back({ parameter->type_constraint, parameter });
		}
		known.insert(signal, signature);
		return;
	}
	if (p_node->type == Parser::Node::LAMBDA) {
		const auto *lambda = static_cast<const Parser::LambdaNode *>(p_node);
		known.insert(lambda, function(lambda->function));
		producers.insert(lambda);
		for (uint32_t i = 0; i < lambda->captures.size(); i++) {
			link(lambda->captures[i], lambda->function->parameters[i]);
		}
		return;
	}
	if (p_node->type == Parser::Node::VARIABLE || p_node->type == Parser::Node::PARAMETER) {
		const auto *value = static_cast<const Parser::AssignableNode *>(p_node);
		if (contains_signature(value->type_constraint)) {
			link(value->initializer, p_node);
		}
		if (p_node->type == Parser::Node::VARIABLE) {
			const auto *variable = static_cast<const Parser::VariableNode *>(p_node);
			if (contains_signature(variable->type_constraint) && variable->getter) {
				link(variable->getter, variable);
			}
		}
		return;
	}
	if (p_node->type == Parser::Node::AWAIT) {
		const auto *value = static_cast<const Parser::AwaitNode *>(p_node)->to_await;
		if (value->type_constraint.is_coroutine && contains_signature(value->type_constraint)) {
			link(value, p_node);
		}
		return;
	}
	if (p_node->type == Parser::Node::RETURN) {
		const auto *ret = static_cast<const Parser::ReturnNode *>(p_node);
		const auto *function_node = scopes[p_node].function;
		if (function_node && contains_signature(function_node->return_type_constraint)) {
			link(ret->return_value, function_node);
		}
		return;
	}
	if (p_node->type == Parser::Node::FOR) {
		const auto *loop = static_cast<const Parser::ForNode *>(p_node);
		if (contains_signature(loop->variable->type_constraint)) {
			link(loop->list, loop->variable);
		}
		return;
	}
	if (!p_node->is_expression()) {
		return;
	}
	if (signal_emit_source(p_node)) {
		// Signal.emit's builtin metadata is variadic. Its native callable uses
		// the signal declaration, which may resolve in a later propagation pass.
		producers.insert(p_node);
		return;
	}
	const auto *expr = static_cast<const Parser::ExpressionNode *>(p_node);
	if (const auto *declaration = source(p_node)) {
		if (declaration->type == Parser::Node::FUNCTION) {
			known.insert(p_node, function(static_cast<const Parser::FunctionNode *>(declaration)));
			producers.insert(p_node);
		} else if (contains_signature(expr->type_constraint)) {
			link(declaration, p_node);
		}
	}
	// Engine metadata is decoded by the same analyzer that checked the script.
	if (p_node->type != Parser::Node::CALL && contains_signature(expr->type_constraint) && !expr->type_constraint.method_info.name.is_empty() && !known.has(p_node) && !source(p_node)) {
		const MethodInfo &info = expr->type_constraint.method_info;
		Parser *parser = nullptr;
		for (const auto &reference : project.parsers) {
			if (reference->get_path() == scopes[p_node].owner->script_path) {
				parser = reference->get_parser();
				break;
			}
		}
		DEV_ASSERT(parser);
		GDScriptAnalyzer analyzer(parser);
		Signature signature;
		signature.signal = expr->type_constraint.builtin_type == Variant::SIGNAL;
		signature.result = { signature.signal ? builtin(Variant::NIL) : analyzer.type_from_property(info.return_val, false, p_node), p_node };
		for (const auto &argument : info.arguments) {
			signature.arguments.push_back({ analyzer.type_from_property(argument, true, p_node), p_node });
		}
		signature.defaults = info.default_arguments.size();
		known.insert(p_node, signature);
	}
	if (p_node->type == Parser::Node::ASSIGNMENT) {
		const auto *assignment = static_cast<const Parser::AssignmentNode *>(p_node);
		if (contains_signature(assignment->assignee->type_constraint)) {
			link(assignment->assigned_value, assignment->assignee);
		}
	} else if (p_node->type == Parser::Node::TERNARY_OPERATOR) {
		const auto *ternary = static_cast<const Parser::TernaryOpNode *>(p_node);
		if (contains_signature(expr->type_constraint)) {
			link(ternary->true_expr, expr);
			link(ternary->false_expr, expr);
		}
	} else if (p_node->type == Parser::Node::ARRAY) {
		if (contains_signature(expr->type_constraint)) {
			for (const auto *element : static_cast<const Parser::ArrayNode *>(p_node)->elements) {
				link(element, expr);
			}
		}
	} else if (p_node->type == Parser::Node::CALL) {
		const auto *call = static_cast<const Parser::CallNode *>(p_node);
		const auto *base = call->get_callee_type() == Parser::Node::SUBSCRIPT ? static_cast<const Parser::SubscriptNode *>(call->callee)->base : nullptr;
		if (call->function_name == SNAME("call_group_as") && base && ClassDB::is_parent_class(base->type_constraint.native_type, SNAME("SceneTree")) && call->arguments.size() >= 2) {
			const auto *declaration = member(call->arguments[0]->type_constraint, call->arguments[1]->reduced_value);
			if (declaration && declaration->type == Parser::Node::FUNCTION) {
				const auto *method = static_cast<const Parser::FunctionNode *>(declaration);
				for (uint32_t i = 2; i < call->arguments.size() && i - 2 < method->parameters.size(); i++) {
					if (contains_signature(method->parameters[i - 2]->type_constraint)) {
						link(call->arguments[i], method->parameters[i - 2]);
					}
				}
			}
			return;
		}
		if (base && base->type_constraint.builtin_type == Variant::CALLABLE && (call->function_name == SNAME("bind") || call->function_name == SNAME("unbind"))) {
			producers.insert(call);
		}
		if (base && base->type_constraint.builtin_type == Variant::CALLABLE && (call->function_name == SNAME("call") || call->function_name == SNAME("call_deferred")) && !known.has(base) && !signal_emit_source(base)) {
			Signature demand;
			demand.priority = 2;
			demand.result = { discarded.has(call) ? builtin(Variant::NIL) : call->type_constraint, call };
			for (const auto *argument : call->arguments) {
				demand.arguments.push_back({ argument->type_constraint, argument });
			}
			// Even a Variant result supplies concrete invocation argument types.
			// The producer still resolves the actual callback return type.
			this->demand(base, demand);
		}
		const auto *declaration = member(call->is_super ? scopes[p_node].owner->node->base_type : base ? base->type_constraint
																									   : scopes[p_node].owner->node->self_type,
				(base && base->type_constraint.is_meta_type && call->function_name == SNAME("new")) ? SNAME("_init") : call->function_name);
		if (declaration && declaration->type == Parser::Node::FUNCTION) {
			const auto *method = static_cast<const Parser::FunctionNode *>(declaration);
			for (uint32_t i = 0; i < call->arguments.size() && i < method->parameters.size(); i++) {
				if (contains_signature(method->parameters[i]->type_constraint)) {
					link(call->arguments[i], method->parameters[i]);
				}
			}
			if (contains_signature(method->return_type_constraint)) {
				link(method, call);
			}
		} else if (base && base->type_constraint.builtin_type == Variant::ARRAY && contains_signature(base->type_constraint)) {
			using APIType = WGodotCppArrayAPI::Type;
			if (const auto *method = WGodotCppArrayAPI::find(call->function_name, call->arguments.size())) {
				if (method->result == APIType::ARRAY || method->result == APIType::ELEMENT) {
					link(base, call);
				}
				for (uint32_t i = 0; i < call->arguments.size(); i++) {
					if (method->arguments[i] == APIType::ELEMENT || method->arguments[i] == APIType::ARRAY) {
						link(call->arguments[i], base);
					}
				}
			}
		}
	}
}

bool WGodotCppSignatures::demand(const Parser::Node *p_node, const Signature &p_signature) {
	const auto *previous = demands.getptr(p_node);
	if (previous && previous->priority >= p_signature.priority) {
		return false;
	}
	Signature copy = p_signature;
	demands.insert(p_node, copy);
	return true;
}

void WGodotCppSignatures::analyze() {
	known.clear();
	demands.clear();
	scopes.clear();
	nodes.clear();
	links.clear();
	discarded.clear();
	producers.clear();
	for (const auto &owner : project.get_classes()) {
		collect(owner.node, { &owner, nullptr });
	}
	for (const auto *node : nodes) {
		seed(node);
	}
	HashSet<const Parser::Node *> linked_arguments;
	// Every iteration resolves an unknown signature or propagates a stronger call-site constraint. There
	// is no arbitrary iteration limit or mutation of GDScript's inferred types.
	bool changed;
	do {
		changed = false;
		for (const Link &edge : links) {
			if (const auto *usage = demands.getptr(edge.target); usage && edge.reverse) {
				changed |= demand(edge.source, *usage);
			}
			if (const auto *usage = demands.getptr(edge.source)) {
				changed |= demand(edge.target, *usage);
			}
			const auto *a = known.getptr(edge.source);
			const auto *b = known.getptr(edge.target);
			// Declarations and assignments can supply the missing argument types
			// of an unbind expression, without replacing its actual return type.
			if (a) {
				changed |= demand(edge.target, *a);
			}
			if (b && edge.reverse) {
				changed |= demand(edge.source, *b);
			}
			if (a && (!b || (!producers.has(edge.target) && a->priority > b->priority))) {
				Signature copy = *a;
				known.insert(edge.target, copy);
				changed = true;
			} else if (b && (!a || b->priority > a->priority) && edge.reverse && !producers.has(edge.source)) {
				Signature copy = *b;
				known.insert(edge.source, copy);
				changed = true;
			}
		}
		for (const auto *node : nodes) {
			if (!known.has(node) && !producers.has(node)) {
				if (const auto *usage = demands.getptr(node); usage && !usage->result.type.is_variant()) {
					known.insert(node, *usage);
					changed = true;
				}
			}
			if (const auto *signal = signal_emit_source(node)) {
				if (const Signature *input = get(signal); input && !known.has(node)) {
					Signature output = *input;
					output.signal = false;
					known.insert(node, output);
					changed = true;
				}
				continue;
			}
			if (node->type != Parser::Node::CALL) {
				continue;
			}
			const auto *call = static_cast<const Parser::CallNode *>(node);
			if (call->get_callee_type() != Parser::Node::SUBSCRIPT) {
				continue;
			}
			const auto *base = static_cast<const Parser::SubscriptNode *>(call->callee)->base;
			const bool binding = base->type_constraint.builtin_type == Variant::CALLABLE && call->function_name == SNAME("bind");
			const bool invocation = (base->type_constraint.builtin_type == Variant::CALLABLE && (call->function_name == SNAME("call") || call->function_name == SNAME("call_deferred"))) ||
					(base->type_constraint.builtin_type == Variant::SIGNAL && call->function_name == SNAME("emit"));
			if ((binding || invocation) && !linked_arguments.has(call)) {
				if (const auto *input = get(base)) {
					// Higher-order arguments retain their declaration origins through
					// bind and invocation, just as arguments to direct script calls do.
					// The receiver can resolve in a later pass, so link only once known.
					linked_arguments.insert(call);
					const int offset = binding ? input->arguments.size() - int(call->arguments.size()) : 0;
					for (uint32_t i = 0; offset >= 0 && i < call->arguments.size() && offset + int(i) < input->arguments.size(); i++) {
						const auto &parameter = input->arguments[offset + i];
						if (contains_signature(parameter.type)) {
							link(call->arguments[i], parameter.origin);
							changed = true;
						}
					}
				}
			}
			if (base->type_constraint.builtin_type == Variant::SIGNAL && !call->arguments.is_empty() &&
					(call->function_name == SNAME("connect") || call->function_name == SNAME("disconnect") || call->function_name == SNAME("is_connected"))) {
				if (const auto *input = get(base)) {
					Signature usage = *input;
					usage.signal = false;
					usage.priority = 2;
					changed |= demand(call->arguments[0], usage);
				}
			}
			if (base->type_constraint.builtin_type != Variant::CALLABLE || known.has(node)) {
				continue;
			}
			const auto *usage = demands.getptr(node);
			if (call->function_name == SNAME("bind") && usage) {
				Signature input = *usage;
				for (const auto *argument : call->arguments) {
					input.arguments.push_back({ argument->type_constraint, argument });
				}
				changed |= demand(base, input);
			}
			if (call->function_name == SNAME("unbind") && usage && call->arguments.size() == 1) {
				const auto *count = call->arguments[0];
				if (count->is_constant && count->reduced && count->reduced_value.get_type() == Variant::INT) {
					const int64_t ignored = count->reduced_value;
					if (ignored > 0 && ignored <= usage->arguments.size()) {
						Signature input = *usage;
						input.arguments.resize(input.arguments.size() - ignored);
						changed |= demand(base, input);
					}
				}
				usage = demands.getptr(node);
			}
			const Signature *input = get(base);
			if (!input) {
				continue;
			}
			if (call->function_name == SNAME("bind") && base->type_constraint.builtin_type == Variant::CALLABLE && call->arguments.size() <= uint32_t(input->arguments.size())) {
				Signature output = *input;
				output.arguments.resize(output.arguments.size() - call->arguments.size());
				output.defaults = MAX(0, input->defaults - int(call->arguments.size()));
				known.insert(call, output);
				changed = true;
			} else if (call->function_name == SNAME("unbind") && usage && call->arguments.size() == 1) {
				const auto *count = call->arguments[0];
				if (!count->is_constant || !count->reduced || count->reduced_value.get_type() != Variant::INT) {
					continue;
				}
				const int64_t ignored = count->reduced_value;
				if (ignored <= 0 || ignored > usage->arguments.size()) {
					continue;
				}
				Signature output = *usage;
				output.result = input->result;
				output.defaults = 0;
				output.signal = false;
				// The source owns the result; the use only supplies argument types.
				output.priority = 3;
				known.insert(call, output);
				changed = true;
			}
		}
	} while (changed);
}

const WGodotCppSignatures::Signature *WGodotCppSignatures::get(const Parser::Node *p_node) const {
	return known.getptr(p_node);
}
