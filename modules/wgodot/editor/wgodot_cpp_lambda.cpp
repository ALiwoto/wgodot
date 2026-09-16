// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::lambda(const Parser::LambdaNode *p_lambda) {
	const String name = "lambda_" + itos(p_lambda->start_line) + "_" + itos(p_lambda->start_column);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_lambda.h");
	if (!class_lambdas.has(p_lambda)) {
		class_lambdas.insert(p_lambda);
		const auto *outer_function = current_function;
		const bool outer_failed = function_failed;
		String declaration;
		const String definition = function(p_lambda->function, declaration, name);
		class_lambda_declarations += declaration;
		class_lambda_definitions += definition;
		current_function = outer_function;
		function_failed = function_failed || outer_failed;
	}
	Vector<String> captures;
	for (const auto *capture : p_lambda->captures) {
		captures.push_back(expression(capture));
	}
	Vector<String> defaults;
	for (const auto *parameter : p_lambda->function->parameters) {
		if (parameter->initializer) {
			if (!parameter->initializer->is_constant) {
				unsupported(parameter, "lambda defaults that depend on a capture or instance");
				return String();
			}
			defaults.push_back(expression(parameter->initializer));
		}
	}
	const String display_name = p_lambda->has_name() ? String(p_lambda->function->identifier->name) : "<anonymous lambda>";
	return "WGodotNative::lambda_callable<" + itos(captures.size()) + ">(&" + current_class->cpp_name + "::" + name + ", " + (p_lambda->use_self ? "this" : "nullptr") + ", SNAME(" + quoted(display_name) + "), {" + String(", ").join(captures) + "}, {" + String(", ").join(defaults) + "})";
}
