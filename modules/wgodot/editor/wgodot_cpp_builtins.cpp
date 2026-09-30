// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/variant/type_info.h"

#include "wgodot_cpp_builtin_api.gen.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::native_builtin_call(const Parser::CallNode *p_call, const Parser::DataType &p_base_type, Value &r_result) {
	if (is_warray(expression_type(p_call))) {
		return false; // Builtins returning Array[T] need an explicit Godot Array -> WArray result handler.
	}
	const String owner = Variant::get_type_name(p_base_type.builtin_type);
	for (const auto &entry : builtin_methods) {
		if (owner != entry.owner || p_call->function_name != entry.name) {
			continue;
		}
		const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
		const int count = Variant::get_builtin_method_argument_count(p_base_type.builtin_type, p_call->function_name);
		const Vector<Variant> defaults = Variant::get_builtin_method_default_arguments(p_base_type.builtin_type, p_call->function_name);
		Vector<Value> operands;
		for (int i = 0; i < count; i++) {
			const Variant::Type argument_type = Variant::get_builtin_method_argument_type(p_base_type.builtin_type, p_call->function_name, i);
			operands.push_back(i < int(p_call->arguments.size()) ? lower_engine_argument(p_call->arguments[i], argument_type) : lower_literal(defaults[i - (count - defaults.size())], p_call));
		}
		if (!entry.is_static || !p_base_type.is_meta_type) {
			operands.push_back(lower(base));
		}
		r_result = sequence(operands);
		Vector<String> arguments;
		for (int i = 0; i < count; i++) {
			arguments.push_back(operands[i].code);
		}
		r_result.cpp_type = type(expression_type(p_call), p_call);
		const String parameters = arguments.is_empty() ? "" : ", " + String(", ").join(arguments);
		class_call_headers.insert("modules/wgodot/native/wgodot_native_builtin.h");
		if (entry.is_static) {
			if (!p_base_type.is_meta_type) {
				r_result.setup.push_back("(void)(" + operands[count].code + ");");
			}
			r_result.code = "WGodotNative::invoke_static<" + r_result.cpp_type + ">(" + entry.pointer + parameters + ")";
		} else {
			String receiver = operands[count].code;
			if (owner != entry.receiver) {
				receiver = "WGodotNative::convert<" + String(entry.receiver) + ">(" + receiver + ")";
			}
			r_result.code = "WGodotNative::invoke_builtin<" + r_result.cpp_type + ">(" + entry.pointer + ", " + receiver + parameters + ")";
		}
		r_result.effects = true;
		return true;
	}
	return false;
}

String WGodotCppEmitter::native_constructor(Variant::Type p_target, const Vector<Parser::DataType> &p_types, const Vector<String> &p_arguments) {
	for (const auto &source : p_types) {
		if (source.kind != Parser::DataType::BUILTIN && source.kind != Parser::DataType::ENUM) {
			return String();
		}
	}
	// Match Variant::construct's registration order, not C++'s overload ranking.
	// In particular, a convertible signature can precede an exact signature.
	for (int index = 0; index < Variant::get_constructor_count(p_target); index++) {
		if (Variant::get_constructor_argument_count(p_target, index) != p_types.size()) {
			continue;
		}
		bool matches = true;
		for (int i = 0; i < p_types.size(); i++) {
			const Variant::Type source = p_types[i].kind == Parser::DataType::ENUM ? Variant::INT : p_types[i].builtin_type;
			if (!Variant::can_convert_strict(source, Variant::get_constructor_argument_type(p_target, index, i))) {
				matches = false;
				break;
			}
		}
		if (!matches) {
			continue;
		}
		for (const auto &entry : builtin_constructors) {
			if (entry.kind == p_target && entry.index == index) {
				class_call_headers.insert("modules/wgodot/native/wgodot_native_builtin.h");
				const String function = entry.from_string ? "construct_from_string" : "construct_builtin";
				return "WGodotNative::" + function + "<" + entry.parameters + ">(" + String(", ").join(p_arguments) + ")";
			}
		}
		return String();
	}
	return String();
}

String WGodotCppEmitter::native_utility(const Parser::CallNode *p_call, const Vector<Value> &p_arguments) {
	const String result_type = type(expression_type(p_call), p_call);
	if (p_arguments.size() == 1 && p_arguments[0].cpp_type != "Variant") {
		const auto source = expression_type(p_call->arguments[0]);
		const Variant::Type kind = source.kind == Parser::DataType::ENUM ? Variant::INT : source.kind == Parser::DataType::BUILTIN ? source.builtin_type : Variant::VARIANT_MAX;
		for (const auto &entry : unary_utilities) {
			if (p_call->function_name == entry.name && kind == entry.kind) {
				class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
				const String value = "(" + convert_value(p_arguments[0], entry.type) + ")";
				return "WGodotNative::convert<" + result_type + ">(" + String(entry.expression).replace("{value}", value) + ")";
			}
		}
	}
	Vector<String> arguments;
	for (int i = 0; i < p_arguments.size(); i++) {
		const auto source = expression_type(p_call->arguments[i]);
		const bool numeric = source.kind == Parser::DataType::ENUM || (source.kind == Parser::DataType::BUILTIN && (source.builtin_type == Variant::INT || source.builtin_type == Variant::FLOAT));
		arguments.push_back(numeric ? convert_value(p_arguments[i], type(source, p_call->arguments[i])) : p_arguments[i].code);
	}
	for (const auto &entry : builtin_utilities) {
		if (p_call->function_name == entry.name) {
			class_call_headers.insert("core/variant/variant_utility.h");
			class_call_headers.insert("modules/wgodot/native/wgodot_native_builtin.h");
			const String parameters = arguments.is_empty() ? "" : ", " + String(", ").join(arguments);
			if (String(entry.invoke) == "invoke_static") {
				return "WGodotNative::invoke_static<" + result_type + ">(&VariantUtilityFunctions::" + entry.function + parameters + ")";
			}
			return "WGodotNative::" + String(entry.invoke) + "<" + result_type + ", &VariantUtilityFunctions::" + entry.function + ">(" + WGodotCppNames::quoted(entry.name) + parameters + ")";
		}
	}
	return String();
}
