// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_virtuals(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions) {
	List<MethodInfo> virtual_methods;
	ClassDB::get_virtual_methods(native_base(p_class.node->self_type), &virtual_methods);
	String dispatch;
	const bool game_parent = p_class.node->base_type.kind == Parser::DataType::CLASS;
	const String parent = class_name(p_class.node->base_type, p_class.node);
	if (!game_parent) {
		r_declaration += "\tstatic Variant callback_initialize_game(Object *p_self, const Variant **p_args, int p_count);\n";
		r_definitions += "Variant " + p_class.cpp_name + "::callback_initialize_game(Object *p_self, const Variant **p_args, int p_count) {\n\tauto *self = static_cast<" + p_class.cpp_name + " *>(p_self);\n\tself->game_initialized = true;\n\tself->initialize_fields();\n\tif (self->construction_mode == WGodotNative::Construction::SCENE) { self->initialize_default(); }\n\treturn Variant();\n}\n\n";
		dispatch += "\tif (p_name == \"@game_initialize\") { return &callback_initialize_game; }\n";
	}
	// Script notifications visit every script level. Keep their ordering separate
	// from native base notifications, just as Object does for ScriptInstance.
	r_declaration += "protected:\n\tvoid notify_game(int p_what, bool p_reversed);\npublic:\n";
	r_definitions += "void " + p_class.cpp_name + "::notify_game(int p_what, bool p_reversed) {\n\tif (!game_initialized) { return; }\n";
	if (game_parent) {
		r_definitions += "\tif (!p_reversed) { " + parent + "::notify_game(p_what, p_reversed); }\n";
	}
	if (p_class.node->has_function(SNAME("_notification"))) {
		r_definitions += "\t" + p_class.cpp_name + "::m_" + symbol(SNAME("_notification")) + "(p_what);\n";
		r_declaration += "\tstatic Variant callback_notification(Object *p_self, const Variant **p_args, int p_count);\n";
		dispatch += "\tif (p_name == \"_notification\") { return &callback_notification; }\n";
	}
	if (game_parent) {
		r_definitions += "\tif (p_reversed) { " + parent + "::notify_game(p_what, p_reversed); }\n";
	}
	r_definitions += "}\n\n";
	if (p_class.node->has_function(SNAME("_notification"))) {
		r_definitions += "Variant " + p_class.cpp_name + "::callback_notification(Object *p_self, const Variant **p_args, int p_count) {\n\tstatic_cast<" + p_class.cpp_name + " *>(p_self)->notify_game(int(*p_args[0]), bool(*p_args[1]));\n\treturn Variant();\n}\n\n";
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
		if (method.flags & METHOD_FLAG_OBJECT_CORE) {
			unsupported(function, "Object callback " + String(method.name));
			continue;
		}
		if (method.arguments.size() > int(function->parameters.size())) {
			unsupported(function, "native callback signature " + String(method.name));
			continue;
		}
		const String wrapper = "callback_" + symbol(method.name);
		r_declaration += "\tstatic Variant " + wrapper + "(Object *p_self, const Variant **p_args, int p_count);\n";
		r_definitions += "Variant " + p_class.cpp_name + "::" + wrapper + "(Object *p_self, const Variant **p_args, int p_count) {\n";
		Vector<String> arguments;
		for (int i = 0; i < method.arguments.size(); i++) {
			const String argument = "argument_" + itos(i);
			const auto *parameter = function->parameters[i];
			r_definitions += "\tauto " + argument + " = VariantCaster<" + type(parameter->type_constraint, parameter) + ">::cast(*p_args[" + itos(i) + "]);\n";
			arguments.push_back(argument);
		}
		const bool returns_void = function->return_type_constraint.kind == Parser::DataType::BUILTIN && function->return_type_constraint.builtin_type == Variant::NIL;
		const String invoke = "static_cast<" + p_class.cpp_name + " *>(p_self)->m_" + symbol(method.name) + "(" + String(", ").join(arguments) + ")";
		r_definitions += returns_void ? "\t" + invoke + ";\n\treturn Variant();\n" : "\treturn " + invoke + ";\n";
		r_definitions += "}\n\n";
		dispatch += "\tif (p_name == " + quoted(method.name) + ") { return &" + wrapper + "; }\n";
	}
	if (!dispatch.is_empty()) {
		r_declaration += "protected:\n\tWGodotNativeVirtual _wgodot_get_native_virtual(const StringName &p_name) const override;\npublic:\n";
		r_definitions += "Object::WGodotNativeVirtual " + p_class.cpp_name + "::_wgodot_get_native_virtual(const StringName &p_name) const {\n" + dispatch + "\treturn " + class_name(p_class.node->base_type, p_class.node) + "::_wgodot_get_native_virtual(p_name);\n}\n\n";
	}
}
