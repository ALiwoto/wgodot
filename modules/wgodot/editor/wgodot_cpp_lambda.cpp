// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::lambda(const Parser::LambdaNode *p_lambda) {
	const String name = "lambda_" + itos(p_lambda->start_line) + "_" + itos(p_lambda->start_column);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_callback.h");
	if (!class_lambdas.has(p_lambda)) {
		class_lambdas.insert(p_lambda);
		const auto *outer_function = current_function;
		const bool outer_failed = function_failed;
		auto outer_locals = std::move(local_overrides);
		auto outer_expressions = std::move(expression_overrides);
		local_overrides.clear();
		expression_overrides.clear();
		String declaration;
		const String definition = function(p_lambda->function, declaration, name);
		class_lambda_declarations += declaration;
		class_lambda_definitions += definition;
		current_function = outer_function;
		function_failed = function_failed || outer_failed;
		local_overrides = std::move(outer_locals);
		expression_overrides = std::move(outer_expressions);
	}
	Vector<String> captures;
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_lambda->captures.size(); i++) {
		const String capture_name = "capture_" + itos(i);
		captures.push_back(capture_name + " = " + expression(p_lambda->captures[i]));
		arguments.push_back(capture_name);
	}
	if (p_lambda->use_self) {
		captures.push_back("owner = " + type(current_class->node->self_type, p_lambda) + "(this)");
	}
	Vector<String> parameters;
	Vector<String> defaults;
	for (uint32_t i = p_lambda->captures.size(); i < p_lambda->function->parameters.size(); i++) {
		const auto *parameter = p_lambda->function->parameters[i];
		const String argument = "argument_" + itos(i);
		parameters.push_back(type(parameter->type_constraint, parameter) + " " + argument);
		arguments.push_back(argument);
		if (parameter->initializer) {
			if (!parameter->initializer->is_constant) {
				unsupported(parameter, "lambda defaults that depend on a capture or instance");
				return String();
			}
			defaults.push_back(converted(parameter->initializer, parameter->type_constraint, parameter));
		}
	}
	String code = signature_type(p_lambda) + "::make([" + String(", ").join(captures) + "](" + String(", ").join(parameters) + ") -> " + function_result(p_lambda->function) + " { return " + (p_lambda->use_self ? "owner->" : current_class->cpp_name + "::") + name + "(" + String(", ").join(arguments) + "); }";
	if (p_lambda->use_self) {
		code += ", [id = get_instance_id()]() { return ObjectDB::get_instance(id) != nullptr; }, get_instance_id()";
	}
	code += ")";
	if (!defaults.is_empty()) {
		code += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
	}
	return code;
}
