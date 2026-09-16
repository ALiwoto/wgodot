// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"
#include "wgodot_cpp_native_methods.gen.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::initialize_native_methods() {
	for (const auto &entry : native_cpp_methods) {
		native_methods.insert(String(entry.owner) + "::" + entry.name, entry.method);
	}
}

String WGodotCppEmitter::native_adapter(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_vararg) {
	const String key = String(p_owner) + "::" + String(p_name);
	const String name = "NativeCall_" + key.sha256_text().substr(0, 16);
	const String header = name + ".h";
	class_call_headers.insert(header);
	if (files.has(header)) {
		return name;
	}
	const String *method = p_vararg ? nullptr : native_methods.getptr(key);
	String code = "// wgodot-changes::file\n// Generated native call adapter for " + key + ".\n#pragma once\n#include \"modules/wgodot/native/wgodot_native_calls.h\"\n\nnamespace WGodotGame {\nstruct " + name + " {\n";
	if (method) {
		const String invoke = String("WGodotNative::") + (p_static ? "invoke_static" : "invoke_member") + "<Result>(&Instance::" + *method + (p_static ? "" : ", p_self") + ", std::forward<Args>(p_args)...)";
		code += "\ttemplate <class Result, class Instance, class... Args>\n\tstatic auto invoke(int, Instance *p_self, Args &&...p_args) -> decltype(" + invoke + ") {\n\t\treturn " + invoke + ";\n\t}\n";
	}
	// Some bindings expose private wrappers or overloaded methods. Let C++ overload
	// resolution choose the existing native MethodBind when direct access is invalid.
	code += "\ttemplate <class Result, class Instance, class... Args>\n\tstatic Result invoke(long, Instance *p_self, Args &&...p_args) {\n\t\tstatic const MethodBind *method = ClassDB::get_method(SNAME(" + quoted(p_owner) + "), SNAME(" + quoted(p_name) + "));\n\t\treturn WGodotNative::invoke_bind<Result>(method, " + (p_static ? "nullptr" : "p_self") + ", std::forward<Args>(p_args)...);\n\t}\n};\n} // namespace WGodotGame\n";
	files.insert(header, code);
	return name;
}

String WGodotCppEmitter::native_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const Parser::DataType &p_base_type) {
	if (p_base_type.kind != Parser::DataType::CLASS && p_base_type.kind != Parser::DataType::NATIVE) {
		unsupported(p_call, "callable invocation " + String(p_call->function_name));
		return String();
	}
	const StringName base_name = native_base(p_base_type);
	if (p_call->function_name == SNAME("free") && p_call->arguments.is_empty() && !p_base_type.is_meta_type) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "WGodotNative::free_object(WGodotNative::object_pointer(" + (p_base ? expression(p_base) : "this") + "))";
	}
	if (p_base && p_base_type.is_meta_type && p_call->function_name == SNAME("new")) {
		if (!p_call->arguments.is_empty() || !ClassDB::can_instantiate(base_name)) {
			unsupported(p_call, "native constructor " + String(base_name));
			return String();
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return "WGodotNative::instantiate<" + class_name(p_base_type, p_call) + ">()";
	}
	const MethodBind *method = ClassDB::get_method(base_name, p_call->function_name);
	if (!method) {
		unsupported(p_call, "unbound native method " + String(base_name) + "." + String(p_call->function_name));
		return String();
	}
	String body = "([&]() { ";
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const String argument = "argument_" + itos(i);
		body += "auto &&" + argument + " = " + expression(p_call->arguments[i]) + "; ";
		arguments.push_back(argument);
	}
	String receiver;
	if (method->is_static()) {
		Parser::DataType native_type;
		native_type.kind = Parser::DataType::NATIVE;
		native_type.native_type = method->get_instance_class();
		receiver = "static_cast<" + class_name(native_type, p_call) + " *>(nullptr)";
	} else if (p_base && p_base->type == Parser::Node::IDENTIFIER && static_cast<const Parser::IdentifierNode *>(p_base)->name == SNAME("ResourceLoader") && p_base_type.kind == Parser::DataType::NATIVE && p_base_type.is_meta_type && method->get_name() == SNAME("load_threaded_get_status")) {
		// This singleton operation is emitted as a core static call. Do not
		// generate a runtime singleton-name lookup for its class identifier.
		receiver = "nullptr";
	} else if (p_base) {
		body += "auto &&receiver = " + expression(p_base) + "; ";
		receiver = "WGodotNative::object_pointer(receiver)";
	} else {
		receiver = "this";
	}
	return body + "return " + native_invoke(method, receiver, arguments, type(p_call->type_constraint, p_call), p_call) + "; }())";
}

String WGodotCppEmitter::native_invoke(const MethodBind *p_method, const String &p_receiver, Vector<String> p_arguments, const String &p_result, const Parser::Node *p_origin) {
	String override_code;
	if (native_override(p_method, p_receiver, p_arguments, p_result, p_origin, override_code)) {
		return override_code;
	}
	if (!validate_native_arguments(p_method, p_origin)) {
		return String();
	}
	// Native C++ defaults can differ from the registered GDScript API defaults.
	for (int i = p_arguments.size(); i < p_method->get_argument_count(); i++) {
		if (!p_method->has_default_argument(i)) {
			unsupported(p_origin, "missing native argument " + String(p_method->get_name()));
			return String();
		}
		p_arguments.push_back(literal(p_method->get_default_argument(i), p_origin));
	}
	const String adapter = native_adapter(p_method->get_instance_class(), p_method->get_name(), p_method->is_static(), p_method->is_vararg());
	String result = adapter + "::invoke<" + p_result + ">(0, " + p_receiver;
	for (const String &argument : p_arguments) {
		result += ", " + argument;
	}
	return result + ")";
}

String WGodotCppEmitter::native_property(const Parser::ExpressionNode *p_base, const StringName &p_name, const Parser::ExpressionNode *p_origin, const Parser::ExpressionNode *p_value) {
	const auto &base_type = p_base ? p_base->type_constraint : current_class->node->self_type;
	if (base_type.kind != Parser::DataType::CLASS && base_type.kind != Parser::DataType::NATIVE) {
		unsupported(p_origin, "builtin property " + String(p_name));
		return String();
	}
	const StringName base_name = native_base(base_type);
	if (!p_value && (ClassDB::has_signal(base_name, p_name) || ClassDB::has_method(base_name, p_name))) {
		// A stored Callable bypasses native_invoke when it is invoked later.
		// Do not let it circumvent the generic-container boundary checks.
		if (!ClassDB::has_signal(base_name, p_name) && !validate_native_arguments(ClassDB::get_method(base_name, p_name), p_origin)) {
			return String();
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return String(ClassDB::has_signal(base_name, p_name) ? "Signal" : "Callable") + "(WGodotNative::object_pointer(" + (p_base ? expression(p_base) : "this") + "), SNAME(" + quoted(p_name) + "))";
	}
	String body = "([&]() { ";
	String receiver = "this";
	if (p_base) {
		body += "auto &&receiver = " + expression(p_base) + "; ";
		receiver = "receiver";
	}
	if (p_value) {
		body += "auto &&value = " + expression(p_value) + "; ";
	}
	return body + "return " + property_access(base_type, p_name, p_origin, receiver, p_value ? "value" : "") + "; }())";
}
