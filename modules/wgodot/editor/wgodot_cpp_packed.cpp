// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_packed_api.gen.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::is_packed(const Parser::DataType &p_type) const {
	return !p_type.is_coroutine && p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY;
}

String WGodotCppEmitter::packed_element_type(Variant::Type p_type) const {
	for (const auto &entry : packed_types) {
		if (entry.kind == p_type) {
			return entry.element;
		}
	}
	ERR_FAIL_V_MSG(String(), "Missing native packed-array element type.");
}

WGodotCppEmitter::Value WGodotCppEmitter::packed_call(const Parser::CallNode *p_call) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	const auto base_type = expression_type(base);
	const String owner = Variant::get_type_name(base_type.builtin_type);
	for (const auto &entry : packed_methods) {
		if (owner != entry.owner || p_call->function_name != entry.name) {
			continue;
		}
		if (!validate_builtin_arguments(base_type.builtin_type, p_call->function_name, p_call)) {
			return Value();
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
		Vector<Value> operands;
		const int count = Variant::get_builtin_method_argument_count(base_type.builtin_type, p_call->function_name);
		const Vector<Variant> defaults = Variant::get_builtin_method_default_arguments(base_type.builtin_type, p_call->function_name);
		for (int i = 0; i < count; i++) {
			const Variant::Type argument_type = Variant::get_builtin_method_argument_type(base_type.builtin_type, p_call->function_name, i);
			operands.push_back(i < int(p_call->arguments.size()) ? lower_engine_argument(p_call->arguments[i], argument_type) : lower_literal(defaults[i - (count - defaults.size())], p_call));
		}
		operands.push_back(lower_converted(base, base_type));
		Value result = sequence(operands);
		Vector<String> arguments;
		for (int i = 0; i < count; i++) {
			arguments.push_back(operands[i].code);
		}
		const String receiver = "(" + operands[count].code + ").packed_native()";
		result.cpp_type = type(expression_type(p_call), p_call);
		const String parameters = arguments.is_empty() ? "" : ", " + String(", ").join(arguments);
		if (entry.member[0]) {
			result.code = "WGodotNative::invoke_member<" + result.cpp_type + ">(" + entry.member + ", &(" + receiver + ")" + parameters + ")";
		} else {
			class_call_headers.insert("modules/wgodot/native/wgodot_native_packed_api.gen.h");
			result.code = "WGodotNative::invoke_static<" + result.cpp_type + ">(&WGodotNative::" + entry.function + ", " + receiver + parameters + ")";
		}
		result.effects = true;
		return result;
	}
	unsupported(p_call, "packed-array method without a generated native mapping: " + String(p_call->function_name));
	return Value();
}

WGodotCppEmitter::Value WGodotCppEmitter::packed_array(const Parser::ExpressionNode *p_source, const Parser::DataType &p_target) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	Value result;
	if (p_source->type == Parser::Node::ARRAY) {
		Vector<Value> values;
		for (const auto *element : static_cast<const Parser::ArrayNode *>(p_source)->elements) {
			values.push_back(lower(element));
		}
		result = sequence(values);
		Vector<String> elements;
		for (const Value &value : values) {
			elements.push_back(convert_value(value, packed_element_type(p_target.builtin_type)));
		}
		result.code = type(p_target, p_source) + "{ " + String(", ").join(elements) + " }";
	} else {
		result = lower(p_source);
		result.code = "WGodotNative::copy_packed<" + type(p_target, p_source) + ">(" + result.code + ")";
	}
	result.cpp_type = type(p_target, p_source);
	result.storage_type = String();
	result.borrowed = false;
	result.read_only = false;
	result.invariant = false;
	result.effects = true;
	return result;
}
