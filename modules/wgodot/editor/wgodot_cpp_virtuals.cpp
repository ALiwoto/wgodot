// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_virtuals(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions) {
	List<MethodInfo> virtual_methods;
	ClassDB::get_virtual_methods(native_base(p_class.node->self_type), &virtual_methods);
	const bool game_parent = p_class.node->base_type.kind == Parser::DataType::CLASS;
	const String parent = class_name(p_class.node->base_type, p_class.node);
	const ClassLifecycle &lifecycle = class_lifecycles[p_class.node];
	const ClassLifecycle inherited = game_parent ? class_lifecycles[p_class.node->base_type.class_type] : ClassLifecycle();
	// An inherited hook already dispatches overridden initialization helpers.
	// Replace it only when a descendant introduces another kind of lifecycle work.
	if (lifecycle.fields != inherited.fields || lifecycle.constructor != inherited.constructor || lifecycle.notifications != inherited.notifications) {
		r_declaration += "protected:\n\tvoid _wgodot_native_initialize() override;\npublic:\n";
		r_definitions += "void " + p_class.cpp_name + "::_wgodot_native_initialize() {\n";
		if (lifecycle.notifications) {
			r_definitions += "\tgame_initialized = true;\n";
		}
		if (lifecycle.fields) {
			r_definitions += "\tinitialize_fields();\n";
		}
		if (lifecycle.constructor) {
			r_definitions += "\tif (construction_mode == WGodotNative::Construction::SCENE) { initialize_default(); }\n";
		}
		r_definitions += "}\n\n";
	}
	if (lifecycle.tasks && !inherited.tasks) {
		r_declaration += "protected:\n\tvoid _wgodot_native_clear() override;\npublic:\n";
		r_definitions += "void " + p_class.cpp_name + "::_wgodot_native_clear() {\n\tgame_tasks.clear();\n}\n\n";
	}
	// Script notifications visit every script level. Keep their ordering separate
	// from native base notifications, just as Object does for ScriptInstance.
	if (p_class.node->has_function(SNAME("_notification")) && !p_class.node->get_member(SNAME("_notification")).function->is_abstract) {
		r_declaration += "protected:\n\tvoid _wgodot_native_notification(int p_what, bool p_reversed) override;\npublic:\n";
		r_definitions += "void " + p_class.cpp_name + "::_wgodot_native_notification(int p_what, bool p_reversed) {\n\tif (!game_initialized) { return; }\n";
		if (inherited.notifications) {
			r_definitions += "\tif (!p_reversed) { " + parent + "::_wgodot_native_notification(p_what, p_reversed); }\n";
		}
		r_definitions += "\t" + p_class.cpp_name + "::m_" + symbol(SNAME("_notification")) + "(p_what);\n";
		if (inherited.notifications) {
			r_definitions += "\tif (p_reversed) { " + parent + "::_wgodot_native_notification(p_what, p_reversed); }\n";
		}
		r_definitions += "}\n\n";
	}
	HashSet<StringName> emitted;
	for (const MethodInfo &method : virtual_methods) {
		// Object exposes _init as script metadata; native factories invoke it themselves.
		if (method.name == SNAME("_init") || method.name == SNAME("_notification")) {
			continue;
		}
		if (emitted.has(method.name) || !p_class.node->has_function(method.name)) {
			continue;
		}
		emitted.insert(method.name);
		const auto *function = p_class.node->get_member(method.name).function;
		if (method.name == SNAME("_to_string")) {
			// Object::to_string() already dispatches this real C++ virtual.
			// Keep the generated method for direct script calls and super calls;
			// this bridge also preserves dispatch through generated subclasses.
			if (function->is_coroutine) {
				unsupported(function, "asynchronous Object callback _to_string; Object::to_string() requires an immediate String result");
				continue;
			}
			r_declaration += "protected:\n\tString _to_string() override;\npublic:\n";
			r_definitions += "String " + p_class.cpp_name + "::_to_string() {\n\treturn m_" + symbol(method.name) + "();\n}\n\n";
			continue;
		}
		if (has_native_value_signature(function)) {
			unsupported(function, "native callback " + String(method.name) + " with a native container/callback/signal signature requires an explicit boundary adapter");
			continue;
		}
		if (method.flags & METHOD_FLAG_OBJECT_CORE) {
			unsupported(function, "Object callback " + String(method.name));
			continue;
		}
		if (method.arguments.size() > int(function->parameters.size())) {
			unsupported(function, "native callback signature " + String(method.name));
			continue;
		}
		const String wrapper = "_wgodot_native_" + String(method.name);
		const String qualifier = (method.flags & METHOD_FLAG_CONST) ? " const" : "";
		const bool returns_value = method.return_val.type != Variant::NIL || (method.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
		const bool returns_void = function->return_type_constraint.kind == Parser::DataType::BUILTIN && function->return_type_constraint.builtin_type == Variant::NIL;
		Vector<String> parameters;
		Vector<String> arguments;
		for (int i = 0; i < method.arguments.size(); i++) {
			const String argument = "argument_" + itos(i);
			const auto *parameter = function->parameters[i];
			parameters.push_back(wrapper + "_arg" + itos(i + 1) + " " + argument);
			arguments.push_back("WGodotNative::virtual_argument<" + type(parameter->type_constraint, parameter) + ">(" + argument + ")");
		}
		if (returns_value) {
			parameters.push_back(wrapper + "_result &r_ret");
		}
		r_declaration += "protected:\n\tbool " + wrapper + "(" + String(", ").join(parameters) + ")" + qualifier + " override;\n";
		r_declaration += "\tbool " + wrapper + "_overridden() const override { return true; }\npublic:\n";
		r_definitions += "bool " + p_class.cpp_name + "::" + wrapper + "(" + String(", ").join(parameters) + ")" + qualifier + " {\n";
		// Script methods have no C++ const qualifier, even for const engine callbacks.
		const String receiver = qualifier.is_empty() ? "" : "const_cast<" + p_class.cpp_name + " *>(this)->";
		const String invoke = receiver + "m_" + symbol(method.name) + "(" + String(", ").join(arguments) + ")";
		if (returns_value && !returns_void) {
			r_definitions += "\tr_ret = WGodotNative::convert<" + wrapper + "_result>(" + invoke + ");\n";
		} else {
			r_definitions += "\t" + invoke + ";\n";
			if (returns_value) {
				r_definitions += "\tr_ret = {};\n";
			}
		}
		r_definitions += "\treturn true;\n";
		r_definitions += "}\n\n";
		if (!arguments.is_empty() || returns_value) {
			class_call_headers.insert("modules/wgodot/native/wgodot_native_virtual.h");
		}
	}
}
