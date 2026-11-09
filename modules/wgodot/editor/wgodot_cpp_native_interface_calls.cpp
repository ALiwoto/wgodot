// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

WGodotCppEmitter::Value WGodotCppEmitter::native_interface_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const WGodotNativeInterfaces::Descriptor &p_interface) {
	const MethodInfo &method = p_interface.methods[p_call->function_name];
	if (method.return_val.type == Variant::DICTIONARY && p_call != engine_dictionary_source) {
		unsupported(p_call, "native interface Dictionary result from " + String(p_call->function_name) + "; this API needs an explicit WDictionary result handler");
		return Value();
	}
	class_call_headers.insert(p_interface.cpp_header);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
	const String result_type = method.return_val.type == Variant::DICTIONARY ? "Dictionary" : type(p_call->type_constraint, p_call);
	Vector<Value> operands;
	int index = 0;
	for (const PropertyInfo &parameter : method.arguments) {
		const bool supplied = index < int(p_call->arguments.size());
		if (supplied && ((parameter.type == Variant::ARRAY && !is_array_duplicate(p_call->arguments[index])) || (parameter.type == Variant::DICTIONARY && !is_dictionary_duplicate(p_call->arguments[index])))) {
			unsupported(p_call, "native interface container argument " + String(p_call->function_name) + "; pass .duplicate() or add an explicit native handler");
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
			value = lower_literal(method.default_arguments[default_index], p_call);
			value.effects = false;
			value.invariant = true;
		}
		operands.push_back(value);
		index++;
	}
	if (p_interface.factory_methods.has(p_call->function_name)) {
		Value result = sequence(operands);
		Vector<String> arguments;
		for (int i = 0; i < operands.size(); i++) {
			arguments.push_back(convert_value(operands[i], native_argument_type(method.arguments[i], p_call)));
		}
		if (p_base && !p_base->type_constraint.is_meta_type && !p_call->is_super) {
			result.setup.push_back("(void)(" + lower(p_base).expression() + ");");
		}
		const String model = class_name(p_call->type_constraint, p_call);
		class_call_headers.insert(model + ".h");
		result.code = p_interface.cpp_type + "::" + String(p_call->function_name) + "<" + model + ">(" + String(", ").join(arguments) + ")";
		result.cpp_type = result_type;
		result.effects = true;
		return result;
	}
	operands.push_back(lower_receiver(p_base));
	Value result = sequence(operands);
	Vector<String> arguments;
	for (int i = 0; i < operands.size() - 1; i++) {
		arguments.push_back(convert_value(operands[i], native_argument_type(method.arguments[i], p_call)));
	}
	Value receiver = operands[operands.size() - 1];
	if (!receiver.borrowed && !receiver.object_pointer) {
		materialize(receiver, result.setup);
	}
	const auto &base_type = p_base ? p_base->type_constraint : current_class->node->self_type;
	const String pointer = is_interface_type(base_type) ? receiver.code + ".operator->()" : "static_cast<" + p_interface.cpp_type + " *>(WGodotNative::object_pointer(" + receiver.code + "))";
	const String instance = materialize_receiver(result, receiver, pointer);
	const String qualification = p_call->wgodot_super_type ? p_interface.cpp_type + "::" : "";
	const String invoke = instance + "->" + qualification + String(p_call->function_name) + "(" + String(", ").join(arguments) + ")";
	result.code = result_type == "void" ? invoke : convert_value(native_result_value(invoke, method.return_val, method.get_argument_meta(-1), p_call), result_type);
	result.cpp_type = result_type;
	result.effects = true;
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::native_interface_member(const Parser::ExpressionNode *p_base, const StringName &p_name, const Parser::ExpressionNode *p_origin, const WGodotNativeInterfaces::Descriptor &p_interface) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
	class_call_headers.insert(p_interface.cpp_header);
	if (p_interface.properties.has(p_name)) {
		return property_access(p_base->type_constraint, p_name, p_origin, lower_receiver(p_base));
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
	if (p_interface.factory_methods.has(p_name)) {
		const auto &base = p_base ? p_base->type_constraint : current_class->node->self_type;
		const String model = class_name(base, p_origin);
		class_call_headers.insert(model + ".h");
		const String method_name = p_interface.cpp_type + "::" + String(p_name) + "<" + model + ">";
		const String slot = method_name.sha256_text().substr(0, 16);
		String invoke = "WGodotNative::static_callable<" + signature_type(p_origin) + ">(&" + method_name + ", UINT64_C(0x" + slot + "))";
		Vector<String> defaults;
		for (const Variant &value : method.default_arguments) {
			defaults.push_back(literal(value, p_origin));
		}
		if (!defaults.is_empty()) {
			invoke += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
		}
		Value result(invoke, signature_type(p_origin));
		if (p_base && !base.is_meta_type) {
			result.setup.push_back("(void)(" + lower(p_base).expression() + ");");
		}
		return result;
	}
	const String slot = (String(p_interface.name) + "::" + String(p_name)).sha256_text().substr(0, 16);
	// A script interface may include this native contract's methods. Its own
	// pure C++ definition owns the member pointer used by the callback.
	const auto &base = p_base ? p_base->type_constraint : current_class->node->self_type;
	const String owner = base.kind == Parser::DataType::CLASS && base.class_type->wgodot_is_interface ? interface_cpp_type(base.class_type) : p_interface.cpp_type;
	String receiver = lower_receiver(p_base).expression();
	if (!is_interface_type(base)) {
		used_native_interfaces.insert(p_interface.name);
		const String wrapper = native_interface_name(p_interface.name);
		class_call_headers.insert(wrapper + ".h");
		receiver = wrapper + "(" + receiver + ")";
	}
	String result = "WGodotNative::interface_callable<" + signature_type(p_origin) + ">(" + receiver + ", &" + owner + "::" + String(p_name) + ", UINT64_C(0x" + slot + "))";
	Vector<String> defaults;
	for (const Variant &value : method.default_arguments) {
		defaults.push_back(literal(value, p_origin));
	}
	if (!defaults.is_empty()) {
		result += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
	}
	return result;
}
