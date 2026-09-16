// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "modules/gdscript/wgodot_gd/builtin_alias_resolver.h"
#include "modules/gdscript/wgodot_gd/builtin_class_aliases.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::variant_type(Variant::Type p_type) const {
	return "Variant::Type(" + itos(p_type) + ")";
}

String WGodotCppEmitter::builtin_call(const Parser::CallNode *p_call) {
	const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_call->callee);
	const auto *base = subscript->base;
	auto base_type = expression_type(base);
	// The analyzer resolves a builtin class name locally for static calls; it
	// intentionally leaves the receiver IdentifierNode's datatype unresolved.
	if (base->type == Parser::Node::IDENTIFIER) {
		const StringName name = WGodotGDScriptBuiltinAliasResolver::resolve_class_alias_or_name(static_cast<const Parser::IdentifierNode *>(base)->name);
		const Variant::Type builtin = Parser::get_builtin_type(name);
		if (builtin < Variant::VARIANT_MAX) {
			base_type.kind = Parser::DataType::BUILTIN;
			base_type.builtin_type = builtin;
			base_type.is_meta_type = true;
		}
	}
	if (base_type.kind != Parser::DataType::BUILTIN || !Variant::has_builtin_method(base_type.builtin_type, p_call->function_name)) {
		unsupported(p_call, "builtin call " + String(p_call->function_name));
		return String();
	}
	if (is_warray(base_type) && !base_type.is_meta_type) {
		return warray_call(p_call);
	}
	if (!validate_builtin_arguments(base_type.builtin_type, p_call->function_name, p_call)) {
		return String();
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	String body = "([&]() { ";
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const String argument = "argument_" + itos(i);
		const auto *source = p_call->arguments[i];
		const auto source_type = expression_type(source);
		const bool string_join = base_type.builtin_type == Variant::STRING && p_call->function_name == SNAME("join") && is_warray(source_type) && source_type.get_container_element_type(0).builtin_type == Variant::STRING;
		const Variant::Type target = int(i) < Variant::get_builtin_method_argument_count(base_type.builtin_type, p_call->function_name) ? Variant::get_builtin_method_argument_type(base_type.builtin_type, p_call->function_name, i) : Variant::NIL;
		body += "auto &&" + argument + " = " + (string_join ? expression(source) : engine_argument(source, target)) + "; ";
		arguments.push_back(argument);
	}
	const bool is_static = Variant::is_builtin_method_static(base_type.builtin_type, p_call->function_name);
	body += "auto &&receiver = " + (is_static && base_type.is_meta_type ? type(base_type, base) + "()" : expression(base)) + "; ";
	if (base_type.builtin_type == Variant::STRING && p_call->function_name == SNAME("join") && arguments.size() == 1) {
		const String strings = is_warray(expression_type(p_call->arguments[0])) ? arguments[0] + ".native()" : "WGodotNative::convert<PackedStringArray>(" + arguments[0] + ")";
		return body + "return receiver.join(" + strings + "); }())";
	}
	// Shared containers mutate their existing storage. Signal methods operate on
	// the owning Object. Only non-shared values need to be assigned back.
	const bool write_back = !Variant::is_builtin_method_const(base_type.builtin_type, p_call->function_name) && !Variant::is_type_shared(base_type.builtin_type) && base_type.builtin_type != Variant::SIGNAL;
	const bool array_result = is_warray(expression_type(p_call));
	const bool dictionary_snapshot = array_result && base_type.builtin_type == Variant::DICTIONARY && (p_call->function_name == SNAME("keys") || p_call->function_name == SNAME("values"));
	if (array_result && !dictionary_snapshot) {
		unsupported(p_call, "builtin Array result from " + String(p_call->function_name) + "; this API needs an explicit WArray result handler");
		return String();
	}
	body += "return " + (dictionary_snapshot ? "WGodotNative::copy_array<" + type(p_call->type_constraint, p_call) + ">(" : "") + "WGodotNative::builtin_call<" + (dictionary_snapshot ? "Array" : type(p_call->type_constraint, p_call)) + ", " + (write_back ? "true" : "false") + ">(receiver, SNAME(" + quoted(p_call->function_name) + ")";
	for (const String &argument : arguments) {
		body += ", " + argument;
	}
	return body + (dictionary_snapshot ? ")); }())" : "); }())");
}

String WGodotCppEmitter::global_call(const Parser::CallNode *p_call) {
	StringName name = p_call->function_name;
	const StringName function_alias = WGodotGDScriptBuiltinClassAliases::resolve_function_alias(name);
	const StringName type_alias = WGodotGDScriptBuiltinClassAliases::resolve_alias(name);
	if (!function_alias.is_empty()) {
		name = function_alias;
	} else if (!type_alias.is_empty()) {
		name = type_alias;
	}
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	if (name == SNAME("len") && p_call->arguments.size() == 1 && is_warray(expression_type(p_call->arguments[0]))) {
		return "(" + expression(p_call->arguments[0]) + ").size()";
	}
	if (is_warray(p_call->type_constraint) && Parser::get_builtin_type(name) == Variant::ARRAY) {
		if (p_call->arguments.is_empty()) {
			return type(p_call->type_constraint, p_call) + "()";
		}
		if (p_call->arguments.size() == 1) {
			return converted(p_call->arguments[0], p_call->type_constraint);
		}
	}
	String body = "([&]() { ";
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const String argument = "argument_" + itos(i);
		body += "auto &&" + argument + " = " + engine_argument(p_call->arguments[i], Variant::NIL) + "; ";
		arguments.push_back(argument);
	}
	const String joined = String(", ").join(arguments);
	const Variant::Type builtin = Parser::get_builtin_type(name);
	if (builtin < Variant::VARIANT_MAX) {
		return body + "return WGodotNative::construct<" + type(p_call->type_constraint, p_call) + ", " + variant_type(builtin) + ">(" + joined + "); }())";
	}
	if (Variant::has_utility_function(name)) {
		return body + "return WGodotNative::utility<" + type(p_call->type_constraint, p_call) + ">(SNAME(" + quoted(name) + ")" + (arguments.is_empty() ? "" : ", " + joined) + "); }())";
	}
	if (name == SNAME("len") && arguments.size() == 1) {
		return body + "return WGodotNative::length(" + arguments[0] + "); }())";
	}
	if (name == SNAME("load") && arguments.size() == 1) {
		const auto *path = p_call->arguments[0];
		if (path->is_constant && path->reduced && String(path->reduced_value).get_extension() == "gd") {
			unsupported(p_call, "loading a script as a resource in a native game");
			return String();
		}
		class_call_headers.insert("core/io/resource_loader.h");
		return body + "return WGodotNative::convert<" + type(p_call->type_constraint, p_call) + ">(::ResourceLoader::load(WGodotNative::convert<String>(" + arguments[0] + "))); }())";
	}
	unsupported(p_call, "global call " + String(name));
	return String();
}
