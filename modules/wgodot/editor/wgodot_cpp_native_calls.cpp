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

WGodotCppEmitter::Value WGodotCppEmitter::native_result_value(const String &p_code, const PropertyInfo &p_info, int p_metadata, const Parser::Node *p_origin) {
	Value result(p_code, native_argument_type(p_info, p_origin));
	// Binding types alone lose C++ wrappers and numeric widths. Normalize these
	// before a conditional or auto temporary infers a different representation.
	if (p_info.type == Variant::OBJECT && p_metadata == GodotTypeInfo::METADATA_OBJECT_IS_REQUIRED) {
		result.code = "static_cast<" + result.cpp_type + ">(" + result.code + ")";
	} else if ((p_info.type == Variant::INT || p_info.type == Variant::FLOAT) && !(p_info.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)) && p_metadata != GodotTypeInfo::METADATA_INT_IS_INT64 && p_metadata != GodotTypeInfo::METADATA_REAL_IS_DOUBLE) {
		result.code = result.cpp_type + "(" + result.code + ")";
	}
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::native_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const Parser::DataType &p_base_type) {
	if (p_base_type.kind != Parser::DataType::CLASS && p_base_type.kind != Parser::DataType::NATIVE) {
		unsupported(p_call, "callable invocation " + String(p_call->function_name));
		return Value();
	}
	const StringName base_name = native_base(p_base_type);
	if (p_call->function_name == SNAME("free") && p_call->arguments.is_empty() && !p_base_type.is_meta_type) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		Value receiver = lower_receiver(p_base);
		Value result = receiver;
		result.code = "WGodotNative::free_object(" + receiver_pointer(receiver, p_base_type, p_call) + ")";
		result.cpp_type = "void";
		result.storage_type = String();
		result.object_pointer = result.borrowed = result.invariant = result.nonnull = false;
		result.effects = true;
		return result;
	}
	if (p_base && p_base_type.is_meta_type && p_call->function_name == SNAME("new")) {
		if (!p_call->arguments.is_empty() || !ClassDB::can_instantiate(base_name)) {
			unsupported(p_call, "native constructor " + String(base_name));
			return Value();
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		return Value("WGodotNative::instantiate<" + class_name(p_base_type, p_call) + ">()", type(p_call->type_constraint, p_call));
	}
	const MethodBind *method = ClassDB::get_method(base_name, p_call->function_name);
	if (!method) {
		unsupported(p_call, "unbound native method " + String(base_name) + "." + String(p_call->function_name));
		return Value();
	}
	if (method->get_instance_class() == SNAME("Tween") && method->get_name() == SNAME("tween_property")) {
		return tween_property_call(p_call, p_base);
	}
	Vector<Value> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const Variant::Type target = int(i) < method->get_argument_count() ? method->get_argument_type(i) : Variant::NIL;
		arguments.push_back(lower_engine_argument(p_call->arguments[i], target));
	}
	Value receiver;
	const bool core_loader = method->get_instance_class() == SNAME("ResourceLoader") && method->get_name() == SNAME("load_threaded_get_status");
	if (!method->is_static() && !core_loader) {
		receiver = lower_receiver(p_base);
		// Preserve any narrowing separately from its storage representation.
		if (receiver_needs_cast(receiver, p_base_type, p_call)) {
			if (!receiver.borrowed && !receiver.object_pointer) {
				Vector<String> setup;
				materialize(receiver, setup);
				receiver.setup = setup;
			}
			receiver.code = receiver_pointer(receiver, p_base_type, p_call);
			receiver.cpp_type = class_name(p_base_type, p_call) + " *";
			receiver.storage_type = type(p_base_type, p_call);
			receiver.object_pointer = true;
			receiver.borrowed = false;
		}
	}
	return native_invoke(method, receiver, arguments, type(p_call->type_constraint, p_call), p_call);
}

WGodotCppEmitter::Value WGodotCppEmitter::native_invoke(const MethodBind *p_method, Value p_receiver, Vector<Value> p_arguments, const String &p_result, const Parser::Node *p_origin) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
	Parser::DataType owner_type;
	owner_type.kind = Parser::DataType::NATIVE;
	owner_type.native_type = p_method->get_instance_class();
	NativeCall call;
	call.owner = class_name(owner_type, p_origin);
	const String key = String(p_method->get_instance_class()) + "::" + String(p_method->get_name());
	const String *mapped_method = native_methods.getptr(key);
	call.method = mapped_method ? *mapped_method : String(p_method->get_name());
	call.is_static = p_method->is_static();
	for (int i = 0; i < p_method->get_argument_count(); i++) {
		call.argument_types.push_back(native_argument_type(p_method->get_argument_info(i), p_origin));
	}
	configure_native_call(p_method, p_origin, p_arguments, call);
	if (function_failed || (!call.adapted && !validate_native_arguments(p_method, p_origin))) {
		return Value();
	}
	if (p_method->is_vararg()) {
		unsupported(p_origin, "direct native call " + key + "; no fixed C++ method mapping is available");
		return Value();
	}
	// Always use the registered script defaults, with their actual literal types.
	for (int i = p_arguments.size(); i < call.argument_types.size(); i++) {
		if (!p_method->has_default_argument(i)) {
			unsupported(p_origin, "missing native argument " + String(p_method->get_name()));
			return Value();
		}
		p_arguments.push_back(lower_literal(p_method->get_default_argument(i), p_origin));
	}
	const bool instance_call = !call.is_static || call.receiver_argument;
	Vector<Value> operands(p_arguments);
	if (instance_call) {
		operands.push_back(p_receiver);
	}
	Value result = sequence(operands);
	String pointer;
	if (instance_call) {
		Value receiver = operands[operands.size() - 1];
		// A temporary Ref must outlive the checked call's if-initializer.
		if (!receiver.borrowed && !receiver.object_pointer) {
			materialize(receiver, result.setup);
		}
		pointer = checked_receiver(result, receiver, receiver.object_pointer ? receiver.code : receiver.code + ".ptr()");
	}
	Vector<String> arguments;
	if (call.receiver_argument) {
		arguments.push_back(pointer);
	}
	for (int i = 0; i < p_arguments.size(); i++) {
		arguments.push_back(convert_value(operands[i], call.argument_types[i]));
	}
	result.code = (call.is_static ? call.owner + "::" : pointer + "->") + call.method + "(" + String(", ").join(arguments) + ")";
	result.effects = true;
	if (p_result == "void") {
		result.cpp_type = "void";
		return result;
	}
	Value native = native_result_value(result.code, p_method->get_return_info(), p_method->get_argument_meta(-1), p_origin);
	result.code = native.code;
	result.cpp_type = native.cpp_type;
	const bool array_result = p_method->get_argument_type(-1) == Variant::ARRAY;
	if (array_result && p_origin == iterated_expression) {
		result.code = array_iteration_result(result.code, p_origin);
		result.cpp_type = "WGodotNative::ArrayRange<" + array_iteration_element(static_cast<const Parser::ExpressionNode *>(p_origin)) + ">";
	} else if (array_result && call.snapshot_result) {
		result.code = "WGodotNative::copy_array<" + p_result + ">(" + result.code + ")";
		result.cpp_type = p_result;
	} else if (array_result) {
		unsupported(p_origin, "native Array result from " + key + "; this API needs an explicit WArray result handler");
	} else if (p_method->get_argument_type(-1) == Variant::DICTIONARY) {
		unsupported(p_origin, "native Dictionary result from " + key + "; this API needs an explicit WDictionary result handler");
	} else if (p_method->get_argument_type(-1) == Variant::OBJECT && result.cpp_type.ends_with(" *")) {
		// Keep a borrowed native pointer for immediate member access. A storing
		// context constructs the owning script handle when it needs one.
		result.object_pointer = true;
		result.storage_type = p_result;
	} else if ((p_method->get_argument_type(-1) == Variant::INT || p_method->get_argument_type(-1) == Variant::FLOAT) && (result.cpp_type != p_result || call.adapted)) {
		result.code = p_result + "(" + result.code + ")";
		result.cpp_type = p_result;
	} else if (result.cpp_type != p_result) {
		result.code = convert_value(result, p_result);
		result.cpp_type = p_result;
	}
	return result;
}

WGodotCppEmitter::Value WGodotCppEmitter::native_property(const Parser::ExpressionNode *p_base, const StringName &p_name, const Parser::ExpressionNode *p_origin, const Parser::ExpressionNode *p_value) {
	const auto &base_type = p_base ? p_base->type_constraint : current_class->node->self_type;
	if (base_type.kind != Parser::DataType::CLASS && base_type.kind != Parser::DataType::NATIVE) {
		unsupported(p_origin, "builtin property " + String(p_name));
		return String();
	}
	const StringName base_name = native_base(base_type);
	if (!p_value && p_name == SNAME("tween_property") && ClassDB::is_parent_class(base_name, SNAME("Tween"))) {
		unsupported(p_origin, "storing Tween.tween_property as a Callable; call it directly with a constant property path so native export can generate typed accessors");
		return String();
	}
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
		Value receiver = lower_receiver(p_base);
		Value result = receiver;
		result.code = native_type + (signal ? "(Signal(" : "::from_callable(Callable(") + receiver_pointer(receiver, base_type, p_origin) + ", SNAME(" + quoted(p_name) + ")))";
		result.cpp_type = native_type;
		result.storage_type = String();
		result.object_pointer = result.nonnull = result.borrowed = result.invariant = false;
		result.effects = true;
		return result;
	}
	return property_access(base_type, p_name, p_origin, lower_receiver(p_base), p_value ? lower(p_value) : Value());
}
