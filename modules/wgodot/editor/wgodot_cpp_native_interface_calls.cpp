// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

WGodotCppEmitter::Value WGodotCppEmitter::native_interface_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const WGodotNativeInterfaces::Descriptor &p_interface) {
	const MethodInfo &method = p_interface.methods[p_call->function_name];
	class_call_headers.insert(p_interface.cpp_header);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
	const String traits = "WGodotNative::InterfaceMethod<decltype(&" + p_interface.cpp_type + "::" + String(p_call->function_name) + ")>";
	const String result_type = type(p_call->type_constraint, p_call);
	Vector<Value> operands;
	int index = 0;
	for (const PropertyInfo &parameter : method.arguments) {
		const bool supplied = index < int(p_call->arguments.size());
		if ((parameter.type == Variant::ARRAY && !(supplied && is_array_duplicate(p_call->arguments[index]))) || parameter.type == Variant::DICTIONARY) {
			unsupported(p_call, "native interface container argument " + String(p_call->function_name) + "; use an explicit supported boundary");
			return String();
		}
		Value value;
		if (supplied) {
			value = lower_engine_argument(p_call->arguments[index], parameter.type);
		} else {
			const int default_index = index - (method.arguments.size() - method.default_arguments.size());
			if (default_index < 0) {
				unsupported(p_call, "missing native interface argument");
				return String();
			}
			value = literal(method.default_arguments[default_index], p_call);
			value.effects = false;
			value.invariant = true;
		}
		operands.push_back(value);
		index++;
	}
	operands.push_back(lower(p_base));
	Value result = sequence(operands);
	Vector<String> arguments;
	for (int i = 0; i < operands.size() - 1; i++) {
		arguments.push_back("WGodotNative::convert<" + traits + "::Argument<" + itos(i) + ">>(" + operands[i].code + ")");
	}
	const String invoke = "instance->" + String(p_call->function_name) + "(" + String(", ").join(arguments) + ")";
	result.code = "([&]() -> " + result_type + " { auto &&receiver = " + operands[operands.size() - 1].code + "; auto *instance = receiver.operator->(); ERR_FAIL_NULL_V(instance, (" + result_type + "())); return " + (result_type == "void" ? invoke : "WGodotNative::convert<" + result_type + ">(" + invoke + ")") + "; }())";
	result.cpp_type = result_type;
	result.effects = true;
	return result;
}

String WGodotCppEmitter::native_interface_member(const Parser::ExpressionNode *p_base, const StringName &p_name, const Parser::ExpressionNode *p_origin, const WGodotNativeInterfaces::Descriptor &p_interface) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
	class_call_headers.insert(p_interface.cpp_header);
	if (p_interface.properties.has(p_name)) {
		return property_access(p_base->type_constraint, p_name, p_origin, expression(p_base));
	}
	if (p_interface.signals.has(p_name)) {
		used_native_interfaces.insert(p_interface.name);
		const String value_type = native_interface_name(p_interface.name);
		class_native_headers.insert(value_type + ".h");
		return "WGodotNative::convert<" + value_type + ">(" + expression(p_base) + ").signal_" + symbol(p_name) + "()";
	}
	const MethodInfo &method = p_interface.methods[p_name];
	for (const PropertyInfo &parameter : method.arguments) {
		if (parameter.type == Variant::ARRAY || parameter.type == Variant::DICTIONARY) {
			unsupported(p_origin, "native interface container method reference; use a lambda with an explicit container boundary");
			return String();
		}
	}
	const String slot = (String(p_interface.name) + "::" + String(p_name)).sha256_text().substr(0, 16);
	// A script interface may include this native contract's methods. Its own
	// pure C++ definition owns the member pointer used by the callback.
	const auto &base = p_base->type_constraint;
	const String owner = base.kind == Parser::DataType::CLASS ? interface_cpp_type(base.class_type) : p_interface.cpp_type;
	String result = "WGodotNative::interface_callable<" + signature_type(p_origin) + ">(" + expression(p_base) + ", &" + owner + "::" + String(p_name) + ", UINT64_C(0x" + slot + "))";
	Vector<String> defaults;
	for (const Variant &value : method.default_arguments) {
		defaults.push_back(literal(value, p_origin));
	}
	if (!defaults.is_empty()) {
		result += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
	}
	return result;
}
