// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

const Parser::FunctionNode *WGodotCppEmitter::interface_method(const Parser::CallNode *p_call) const {
	const Parser::ExpressionNode *base = p_call->get_callee_type() == Parser::Node::SUBSCRIPT ? static_cast<const Parser::SubscriptNode *>(p_call->callee)->base : nullptr;
	const auto &base_type = base ? base->type_constraint : current_class->node->self_type;
	if (base_type.kind == Parser::DataType::CLASS && base_type.class_type->wgodot_is_interface && base_type.class_type->has_function(p_call->function_name)) {
		return base_type.class_type->get_member(p_call->function_name).function;
	}
	return nullptr;
}

WGodotCppEmitter::Value WGodotCppEmitter::call(const Parser::CallNode *p_call) {
	const Parser::ExpressionNode *base = nullptr;
	if (p_call->get_callee_type() == Parser::Node::SUBSCRIPT) {
		base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	}
	const Parser::DataType &base_type = p_call->is_super ? current_class->node->base_type : base ? base->type_constraint
																								 : current_class->node->self_type;
	if (const auto *contract = WGodotGDScriptInterfaceHelpers::native_interface_for_member(base_type, p_call->function_name)) {
		if (contract->methods.has(p_call->function_name)) {
			return native_interface_call(p_call, base, *contract);
		}
	}
	if (base_type.kind != Parser::DataType::CLASS) {
		return native_call(p_call, base, base_type);
	}
	if (const auto *contract_method = interface_method(p_call)) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
		const String result_type = type(p_call->type_constraint, p_call);
		Vector<Value> operands;
		for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
			operands.push_back(lower_converted(p_call->arguments[i], contract_method->parameters[i]->type_constraint, contract_method->parameters[i], true));
		}
		for (uint32_t i = p_call->arguments.size(); i < contract_method->parameters.size(); i++) {
			const auto *parameter = contract_method->parameters[i];
			if (!parameter->initializer) {
				unsupported(p_call, "missing interface argument");
				return String();
			}
			operands.push_back(lower_converted(parameter->initializer, parameter->type_constraint, parameter, true));
		}
		operands.push_back(lower(base));
		Value result = sequence(operands);
		Vector<String> arguments;
		for (int i = 0; i < operands.size() - 1; i++) {
			arguments.push_back(operands[i].code);
		}
		Value value = operands[operands.size() - 1];
		if (!value.borrowed && !value.object_pointer) {
			materialize(value, result.setup);
		}
		const String instance = materialize_receiver(result, value, value.code + ".operator->()");
		result.code = instance + "->" + String(p_call->function_name) + "(" + String(", ").join(arguments) + ")";
		result.cpp_type = result_type;
		result.effects = true;
		return result;
	}
	const bool construct = !p_call->is_super && base && base_type.is_meta_type && p_call->function_name == "new";
	const StringName name = construct ? SNAME("_init") : p_call->function_name;
	const auto *owner = member_owner(base_type.class_type, name);
	const Parser::FunctionNode *method = nullptr;
	if (owner && owner->node->get_member(name).type == Parser::ClassNode::Member::FUNCTION) {
		method = owner->node->get_member(name).function;
	}
	if (!construct && !method) {
		return native_call(p_call, base, base_type);
	}
	if (method && method->is_vararg()) {
		unsupported(p_call, "variadic call " + String(name));
		return String();
	}
	if (owner) {
		class_dependencies.insert(owner->cpp_name);
	}
	Vector<Value> operands;
	// GDScript evaluates arguments before the receiver expression.
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const auto *source = p_call->arguments[i];
		operands.push_back(method && i < method->parameters.size() ? lower_converted(source, method->parameters[i]->type_constraint, method->parameters[i], true) : lower(source));
	}
	const bool evaluate_receiver = base && !base_type.is_meta_type && !p_call->is_super;
	if (evaluate_receiver) {
		operands.push_back(lower(base));
	}
	Value result = sequence(operands);
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		arguments.push_back(operands[i].code);
	}
	String receiver;
	if (construct || (method && method->is_static)) {
		if (evaluate_receiver) {
			result.setup.push_back("(void)(" + operands[operands.size() - 1].code + ");");
		}
		receiver = class_name(base_type, p_call) + "::";
	} else if (p_call->is_super) {
		receiver = class_name(base_type, p_call) + "::";
	} else if (base) {
		const Value &value = operands[operands.size() - 1];
		receiver = "(" + (value.object_pointer ? value.code : convert_value(value, type(base_type, base))) + ")->";
	} else {
		receiver = "this->";
	}
	result.code = receiver + (construct ? "create" : "m_" + symbol(name)) + "(" + String(", ").join(arguments) + ")";
	result.cpp_type = construct ? type(p_call->type_constraint, p_call) : function_result(method);
	result.effects = true;
	return result;
}
