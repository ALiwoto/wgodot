// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"
#include "modules/gdscript/wgodot_gd/construction.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

const Parser::FunctionNode *WGodotCppEmitter::interface_method(const Parser::CallNode *p_call) const {
	const Parser::ExpressionNode *base = p_call->get_callee_type() == Parser::Node::SUBSCRIPT ? static_cast<const Parser::SubscriptNode *>(p_call->callee)->base : nullptr;
	const auto &base_type = p_call->wgodot_super_type ? p_call->wgodot_super_type->resolved_type : base ? base->type_constraint
																										: current_class->node->self_type;
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
	const Parser::DataType &base_type = p_call->wgodot_super_type ? p_call->wgodot_super_type->resolved_type : p_call->is_super ? current_class->node->base_type
			: base																												? base->type_constraint
																																: current_class->node->self_type;
	if (const auto *contract = WGodotGDScriptInterfaceHelpers::native_interface_for_member(base_type, p_call->function_name)) {
		if (contract->methods.has(p_call->function_name)) {
			return native_interface_call(p_call, base, *contract);
		}
	}
	if (base_type.kind != Parser::DataType::CLASS) {
		return native_call(p_call, base, base_type);
	}
	const Parser::DataType *default_provider = base_type.class_type->wgodot_default_methods.getptr(p_call->function_name);
	const Parser::FunctionNode *contract_method = interface_method(p_call);
	if (!contract_method && default_provider && default_provider->kind == Parser::DataType::CLASS) {
		contract_method = default_provider->class_type->get_member(p_call->function_name).function;
	}
	if (contract_method) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
		const String result_type = function_result(contract_method);
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
		operands.push_back(lower_receiver(base));
		Value result = sequence(operands);
		Vector<String> arguments;
		for (int i = 0; i < operands.size() - 1; i++) {
			arguments.push_back(operands[i].code);
		}
		Value value = operands[operands.size() - 1];
		if (!value.borrowed && !value.object_pointer) {
			materialize(value, result.setup);
		}
		String invoke;
		if (p_call->wgodot_super_type) {
			const String provider = class_name(base_type, p_call);
			class_call_headers.insert(provider + ".h");
			invoke = provider + "(" + value.code + ").m_" + symbol(p_call->function_name);
		} else if (!base_type.class_type->wgodot_is_interface) {
			const String contract = interface_cpp_type(default_provider->class_type);
			invoke = "static_cast<" + contract + " *>(WGodotNative::object_pointer(" + value.code + "))->" + String(p_call->function_name);
		} else {
			const String instance = materialize_receiver(result, value, value.code + ".operator->()");
			invoke = instance + "->" + String(p_call->function_name);
		}
		result.code = invoke + "(" + String(", ").join(arguments) + ")";
		result.code = trace_call(p_call, result.code);
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
	for (const auto &field : p_call->wgodot_field_initializers) {
		Value value = lower_converted(field.value, field.field->type_constraint, field.field);
		// A later field expression may reassign this expression's source binding.
		value.borrowed = false;
		operands.push_back(value);
	}
	Value result = sequence(operands);
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		arguments.push_back(operands[i].code);
	}
	if (construct && p_call->wgodot_has_field_initializers) {
		// Even pure field reads precede allocation and static initialization.
		for (uint32_t i = p_call->arguments.size(); i < uint32_t(operands.size()); i++) {
			if (!operands[i].borrowed && !operands[i].invariant) {
				materialize(operands.write[i], result.setup);
			}
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_construction.h");
		const String target = class_name(base_type, p_call);
		const auto fields = WGodotGDScriptConstruction::fields(base_type.class_type);
		Vector<String> mask;
		mask.resize(MAX(1, fields.size()));
		mask.fill("false");
		String assignments;
		for (uint32_t i = 0; i < p_call->wgodot_field_initializers.size(); i++) {
			const auto &field = p_call->wgodot_field_initializers[i];
			mask.write[fields.find(field.field)] = "true";
			assignments += "\tobject->v_" + symbol(field.name->name) + " = " + operands[p_call->arguments.size() + i].code + ";\n";
		}
		bool prepare = false;
		for (const auto *node = base_type.class_type; node; node = node->base_type.class_type) {
			prepare |= node->has_function(SNAME("_static_init"));
			for (const auto &member : node->members) {
				prepare |= member.type == Parser::ClassNode::Member::VARIABLE && member.variable->is_static;
			}
		}
		const String instance = "constructed_" + itos(temporary_index++);
		String construction = type(p_call->type_constraint, p_call) + " " + instance + " = [&]() {\n";
		if (prepare) {
			construction += "\t" + target + "::prepare_game_class();\n";
		}
		construction += "\tauto *object = ::new (DefaultAllocator{}) " + target + "(" + String(method ? "WGodotNative::Construction::EXPLICIT" : "") + ");\n";
		construction += assignments;
		construction += "\tconst bool supplied[] = {" + String(", ").join(mask) + "};\n";
		construction += "\tWGodotNative::ConstructionFields fields(object, supplied);\n\treturn _post_initialize(object);\n}();";
		result.setup.push_back(construction);
		if (method) {
			result.setup.push_back(instance + "->m_" + symbol(name) + "(" + String(", ").join(arguments) + ");");
		}
		result.code = instance;
		result.cpp_type = type(p_call->type_constraint, p_call);
		result.effects = true;
		return result;
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
	result.code = trace_call(p_call, result.code);
	result.cpp_type = construct ? type(p_call->type_constraint, p_call) : function_result(method);
	result.effects = true;
	return result;
}
