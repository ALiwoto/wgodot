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
	// These binding wrappers only forward to the public API.
	native_methods.insert("Object::get", "get");
	native_methods.insert("Object::set", "set");
}

String WGodotCppEmitter::native_argument_type(const PropertyInfo &p_info, const Parser::Node *p_origin) {
	if (p_info.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)) {
		String enum_type = p_info.class_name;
		const int separator = enum_type.find(".");
		if (separator >= 0) {
			Parser::DataType owner;
			owner.kind = Parser::DataType::NATIVE;
			owner.native_type = enum_type.substr(0, separator);
			enum_type = class_name(owner, p_origin) + "::" + enum_type.substr(separator + 1);
		}
		return p_info.usage & PROPERTY_USAGE_CLASS_IS_BITFIELD ? "BitField<" + enum_type + ">" : enum_type;
	}
	if (p_info.type == Variant::OBJECT) {
		Parser::DataType object;
		object.kind = Parser::DataType::NATIVE;
		object.native_type = p_info.class_name.is_empty() ? StringName("Object") : p_info.class_name;
		const String name = class_name(object, p_origin);
		return ClassDB::is_parent_class(object.native_type, "RefCounted") ? "Ref<" + name + ">" : name + " *";
	}
	switch (p_info.type) {
		case Variant::NIL:
			return "Variant";
		case Variant::BOOL:
			return "bool";
		case Variant::INT:
			return "int64_t";
		case Variant::FLOAT:
			return "double";
		default:
			return Variant::get_type_name(p_info.type);
	}
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
	if (p_call == iterated_expression && (!p_base || p_base->type == Parser::Node::SELF) && p_call->arguments.is_empty() && !method->is_static()) {
		return native_invoke(method, "this", Vector<String>(), type(p_call->type_constraint, p_call), p_call);
	}
	String body = "([&]() { ";
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const String argument = "argument_" + itos(i);
		const Variant::Type target = int(i) < method->get_argument_count() ? method->get_argument_type(i) : Variant::NIL;
		body += "auto &&" + argument + " = " + engine_argument(p_call->arguments[i], target) + "; ";
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
		body += "auto &&receiver = " + receiver_expression(p_base) + "; ";
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
	const bool array_iteration = p_origin == iterated_expression && p_method->get_argument_type(-1) == Variant::ARRAY;
	const String result_type = array_iteration ? "Array" : p_result;
	if (p_method->get_argument_type(-1) == Variant::ARRAY && !array_iteration) {
		unsupported(p_origin, "native Array result from " + String(p_method->get_instance_class()) + "." + String(p_method->get_name()) + "; this API needs an explicit WArray result handler");
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
	const String key = String(p_method->get_instance_class()) + "::" + String(p_method->get_name());
	const String *mapped_method = native_methods.getptr(key);
	const String method = mapped_method ? *mapped_method : String(p_method->get_name());
	if (p_method->is_vararg()) {
		unsupported(p_origin, "direct native call " + key + "; no fixed C++ method mapping is available");
		return String();
	}
	Parser::DataType owner_type;
	owner_type.kind = Parser::DataType::NATIVE;
	owner_type.native_type = p_method->get_instance_class();
	const String owner = class_name(owner_type, p_origin);
	for (int i = 0; i < p_arguments.size(); i++) {
		p_arguments.write[i] = "WGodotNative::convert<" + native_argument_type(p_method->get_argument_info(i), p_origin) + ">(" + p_arguments[i] + ")";
	}
	String call = (p_method->is_static() ? owner + "::" : "instance->") + method + "(" + String(", ").join(p_arguments) + ")";
	if (array_iteration) {
		call = array_iteration_result(call, p_origin);
	} else if (result_type != "void") {
		call = "WGodotNative::convert<" + result_type + ">(" + call + ")";
	} else {
		call = "(void)(" + call + ")";
	}
	if (p_method->is_static()) {
		return call;
	}
	// Resolve the pointer after the arguments. Keep the owning receiver in the
	// caller's scope, and use the analyzer-resolved owner for narrowed accesses.
	const String returned_type = array_iteration ? "WGodotNative::ArrayRange<" + array_iteration_element(static_cast<const Parser::ExpressionNode *>(p_origin)) + ">" : result_type;
	String body = "([&]() -> " + returned_type + " { auto *instance = static_cast<" + owner + " *>(" + p_receiver + "); ";
	// An ArrayRange needs an empty array rather than a default constructor.
	const String empty_result = array_iteration ? returned_type + "(Array())" : returned_type + "()";
	body += "ERR_FAIL_NULL_V(instance, (" + empty_result + ")); return " + call + "; }())";
	return body;
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
		const bool signal = ClassDB::has_signal(base_name, p_name);
		const String native_type = signature_type(p_origin, signal);
		const auto *signature = signatures.get(p_origin);
		if (signature) {
			for (const auto &argument : signature->arguments) {
				if (native_only(argument.type)) {
					unsupported(p_origin, "engine signal/callback with a native container argument; an explicit boundary adapter is required");
					return String();
				}
			}
		}
		return native_type + (signal ? "(Signal(" : "::from_callable(Callable(") + "WGodotNative::object_pointer(" + (p_base ? receiver_expression(p_base) : "this") + "), SNAME(" + quoted(p_name) + ")))";
	}
	String body = "([&]() { ";
	String receiver = "this";
	if (p_base) {
		body += "auto &&receiver = " + receiver_expression(p_base) + "; ";
		receiver = "receiver";
	}
	if (p_value) {
		body += "auto &&value = " + expression(p_value) + "; ";
	}
	return body + "return " + property_access(base_type, p_name, p_origin, receiver, p_value ? "value" : "") + "; }())";
}
