// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "modules/gdscript/wgodot_value_paths.h"

using Parser = GDScriptParser;

WGodotCppEmitter::Value WGodotCppEmitter::value_container_call(const Parser::CallNode *p_call) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	bool direct_fields = WGodotValuePaths::plain_data(base->type_constraint);
	const Parser::ExpressionNode *direct_root = base;
	while (direct_fields && direct_root->type == Parser::Node::SUBSCRIPT) {
		const auto *field = static_cast<const Parser::SubscriptNode *>(direct_root);
		direct_fields = field->is_attribute && field->base->type_constraint.kind == Parser::DataType::CLASS && field->base->type_constraint.class_type->wgodot_record;
		direct_root = field->base;
	}
	for (int index = 0; direct_fields && index < Variant::get_builtin_method_argument_count(base->type_constraint.builtin_type, p_call->function_name); ++index) {
		direct_fields = Variant::get_builtin_method_argument_type(base->type_constraint.builtin_type, p_call->function_name, index) != Variant::CALLABLE;
	}
	if (direct_fields && direct_root->type == Parser::Node::IDENTIFIER && local_source(static_cast<const Parser::IdentifierNode *>(direct_root))) {
		// C++ record fields are real lvalues. Arguments are still evaluated first
		// by builtin_call's sequencing; no temporary container needs writeback.
		const Value receiver = lower(base);
		expression_overrides.insert(base, receiver.code);
		Value called = builtin_call(p_call);
		expression_overrides.erase(base);
		return called;
	}
	Value result;
	struct Override {
		const Parser::ExpressionNode *node;
		bool present;
		String previous;
	};
	Vector<Override> overrides;
	auto replace = [&](const Parser::ExpressionNode *p_node, const String &p_value) {
		const String *previous = expression_overrides.getptr(p_node);
		overrides.push_back({ p_node, previous != nullptr, previous ? *previous : String() });
		expression_overrides.insert(p_node, p_value);
	};
	auto restore_last = [&]() {
		const Override previous = overrides[overrides.size() - 1];
		if (previous.present) {
			expression_overrides.insert(previous.node, previous.previous);
		} else {
			expression_overrides.erase(previous.node);
		}
		overrides.resize(overrides.size() - 1);
	};
	// Preserve the language's argument-before-receiver order and each path
	// expression exactly once. The final container owns its pending mutation.
	for (const auto *argument : p_call->arguments) {
		Value value = lower(argument);
		value.borrowed = false;
		materialize(value, result.setup);
		replace(argument, value.code);
	}
	Vector<const Parser::SubscriptNode *> path;
	const Parser::ExpressionNode *root = base;
	while (root->type == Parser::Node::SUBSCRIPT) {
		const auto *segment = static_cast<const Parser::SubscriptNode *>(root);
		path.insert(0, segment);
		root = segment->base;
	}
	Value receiver;
	if (!root->type_constraint.is_meta_type) {
		receiver = lower(root);
		if (root == base || root->type_constraint.builtin_type == Variant::OBJECT) {
			receiver.borrowed = false;
		}
		materialize(receiver, result.setup);
		replace(root, receiver.code);
	}
	for (const auto *segment : path) {
		if (!segment->is_attribute) {
			Value key = lower(segment->index);
			key.borrowed = false;
			materialize(key, result.setup);
			replace(segment->index, key.code);
		}
		receiver = lower(segment);
		receiver.borrowed = false;
		materialize(receiver, result.setup);
		replace(segment, receiver.code);
	}
	Value called = builtin_call(p_call);
	result.setup.append_array(called.setup);
	result.cpp_type = type(expression_type(p_call), p_call);
	if (result.cpp_type == "void") {
		result.setup.push_back(called.code + ";");
	} else {
		result.code = "value_result_" + itos(temporary_index++);
		result.setup.push_back(result.cpp_type + " " + result.code + " = " + called.code + ";");
	}
	restore_last(); // The destination must address its owner, not the saved leaf.
	if (base->type == Parser::Node::IDENTIFIER || base->type == Parser::Node::SUBSCRIPT) {
		Parser::IdentifierNode value;
		value.type_constraint = base->type_constraint;
		value.start_line = base->start_line;
		value.start_column = base->start_column;
		replace(&value, receiver.code);
		Parser::AssignmentNode store;
		store.assignee = const_cast<Parser::ExpressionNode *>(base);
		store.assigned_value = &value;
		store.start_line = base->start_line;
		store.start_column = base->start_column;
		const Value written = assignment(&store);
		result.setup.append_array(written.setup);
		if (!written.code.is_empty()) {
			result.setup.push_back(written.code + ";");
		}
	}
	while (!overrides.is_empty()) {
		restore_last();
	}
	result.effects = true;
	return result;
}
