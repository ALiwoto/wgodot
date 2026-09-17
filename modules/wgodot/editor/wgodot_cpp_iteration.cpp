// wgodot-changes::file
#include "wgodot_cpp_ast.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

namespace {
class IteratorAssignments : public WGodotCppAstVisitor {
	const Parser::IdentifierNode *variable;

	bool visit(const Parser::Node *p_node) override {
		if (p_node->type == Parser::Node::ASSIGNMENT) {
			const auto *target = static_cast<const Parser::AssignmentNode *>(p_node)->assignee;
			if (target->type == Parser::Node::IDENTIFIER) {
				const auto *identifier = static_cast<const Parser::IdentifierNode *>(target);
				if (identifier->source == Parser::IdentifierNode::LOCAL_ITERATOR && identifier->bind_source == variable) {
					assigned = true;
				}
			}
		}
		return true;
	}

public:
	bool assigned = false;
	explicit IteratorAssignments(const Parser::IdentifierNode *p_variable) : variable(p_variable) {}
};
} // namespace

String WGodotCppEmitter::receiver_expression(const Parser::ExpressionNode *p_expression) {
	if (p_expression && p_expression->type == Parser::Node::IDENTIFIER && !expression_overrides.has(p_expression)) {
		const auto *source = local_source(static_cast<const Parser::IdentifierNode *>(p_expression));
		if (const String *view = object_views.getptr(source)) {
			(void)class_name(p_expression->type_constraint, p_expression);
			return *view;
		}
	}
	return expression(p_expression);
}

String WGodotCppEmitter::array_iteration_element(const Parser::ExpressionNode *p_expression) {
	const auto element = expression_type(p_expression).get_container_element_type(0);
	return element.kind == Parser::DataType::NATIVE || element.kind == Parser::DataType::CLASS ? class_name(element, p_expression) : type(element, p_expression);
}

String WGodotCppEmitter::array_iteration_result(const String &p_call, const Parser::Node *p_origin) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_iteration.h");
	emitted_array_range = true;
	return "WGodotNative::iterate<" + array_iteration_element(static_cast<const Parser::ExpressionNode *>(p_origin)) + ">(" + p_call + ")";
}

String WGodotCppEmitter::iteration(const Parser::ForNode *p_loop, int p_indent) {
	const auto collection_type = expression_type(p_loop->list);
	const String variable_type = type(p_loop->variable->type_constraint, p_loop->variable);
	const String variable = "v_" + symbol(p_loop->variable->name);
	Value collection;
	bool range = p_loop->list->type_constraint.kind == Parser::DataType::BUILTIN && p_loop->list->type_constraint.builtin_type == Variant::INT;
	bool array_range = false;
	if (p_loop->list->type == Parser::Node::CALL && static_cast<const Parser::CallNode *>(p_loop->list)->function_name == "range") {
		const auto *call = static_cast<const Parser::CallNode *>(p_loop->list);
		Vector<Value> operands;
		for (const auto *argument : call->arguments) {
			operands.push_back(lower(argument));
		}
		Value value = sequence(operands);
		Vector<String> arguments;
		for (const Value &argument : operands) {
			arguments.push_back(argument.code);
		}
		value.cpp_type = "WGodotNative::Range";
		value.code = "WGodotNative::Range(" + String(", ").join(arguments) + ")";
		collection = value;
		range = true;
	} else {
		const auto *outer_expression = iterated_expression;
		const bool outer_range = emitted_array_range;
		iterated_expression = nullptr;
		emitted_array_range = false;
		if (is_warray(collection_type)) {
			iterated_expression = p_loop->list;
		}
		collection = lower(p_loop->list);
		array_range = emitted_array_range;
		iterated_expression = outer_expression;
		emitted_array_range = outer_range;
	}
	if (array_range) {
		const auto element = collection_type.get_container_element_type(0);
		const bool object = element.kind == Parser::DataType::NATIVE || element.kind == Parser::DataType::CLASS;
		const bool same_type = type(element, p_loop->list) == variable_type;
		IteratorAssignments assignments(p_loop->variable);
		if (!assignments.walk(p_loop->loop)) {
			unsupported(p_loop, "syntax while determining iterator variable ownership");
			return String();
		}
		const bool view = object && same_type && !assignments.assigned;
		if (view) {
			object_views.insert(p_loop->variable, variable);
		}
		if (object && !view) {
			collection.code += ".owned()";
		}
		String code = "for (auto " + (same_type ? variable : "iteration_value") + " : " + collection.code + ") {\n";
		if (!same_type) {
			code += "\t" + variable_type + " " + variable + " = WGodotNative::convert<" + variable_type + ">(iteration_value);\n";
		}
		code += suite(p_loop->loop, 1) + "}\n";
		object_views.erase(p_loop->variable);
		return collection.block(code.trim_suffix("\n"), p_indent);
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	const String iterator_type = range ? "WGodotNative::Range" : is_warray(collection_type) ? "WGodotNative::WArrayIterator<" + type(collection_type.get_container_element_type(0), p_loop) + ">"
																							: "WGodotNative::Iterator";
	String code = "for (" + iterator_type + " iterator{" + collection.code + "}; iterator.has_value(); iterator.next()) {\n";
	code += "\t" + variable_type + " " + variable + " = iterator." + (range ? "get()" : "get<" + variable_type + ">()") + ";\n";
	return collection.block(code + suite(p_loop->loop, 1) + "}", p_indent);
}
