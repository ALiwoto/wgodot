// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_reflection(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions) {
	String bindings;
	String default_bindings;
	const String &name = p_class.cpp_name;
	auto boundary_type = [&](const Parser::DataType &p_type, const Parser::Node *p_origin) {
		if (is_warray(p_type)) {
			return String("Array");
		}
		if (is_wdictionary(p_type)) {
			return String("Dictionary");
		}
		if (p_type.kind == Parser::DataType::CLASS || p_type.kind == Parser::DataType::NATIVE) {
			const String owner = class_name(p_type, p_origin);
			return ClassDB::is_parent_class(native_base(p_type), SNAME("RefCounted")) ? "Ref<" + owner + ">" : owner + " *";
		}
		if (is_packed(p_type)) {
			return Variant::get_type_name(p_type.builtin_type);
		}
		return type(p_type, p_origin);
	};
	for (const auto &entry : p_class.node->members) {
		const Parser::Node *member = entry.get_source_node();
		if (!member || !member->wgodot_export_reflection) {
			continue;
		}
		if (p_class.node->wgodot_static_class || p_class.node->wgodot_is_interface) {
			unsupported(member, "reflection on a class without a Godot object instance");
			continue;
		}
		if (entry.type == Parser::ClassNode::Member::FUNCTION) {
			const auto *function = entry.function;
			bool callback_signature = WGodotCppSignatures::contains_signature(function->return_type_constraint);
			for (const auto *parameter : function->parameters) {
				callback_signature |= WGodotCppSignatures::contains_signature(parameter->type_constraint);
			}
			if (callback_signature || function->is_coroutine) {
				unsupported(function, "reflection of a callback/signal or coroutine function signature");
				continue;
			}
			class_call_headers.insert("modules/wgodot/native/wgodot_native_serialization.h");
			const String wrapper = "reflect_" + symbol(entry.get_name());
			Vector<String> parameters;
			Vector<String> arguments;
			Vector<String> names;
			Vector<String> defaults;
			for (int i = 0; i < int(function->parameters.size()); i++) {
				const auto *parameter = function->parameters[i];
				parameters.push_back(boundary_type(parameter->type_constraint, parameter) + " p_" + itos(i));
				arguments.push_back("WGodotNative::deserialize_value<" + type(parameter->type_constraint, parameter) + ">(Variant(p_" + itos(i) + "))");
				names.push_back(quoted(parameter->identifier->name));
				if (parameter->initializer) {
					defaults.push_back("WGodotNative::serialized_value(" + converted(parameter->initializer, parameter->type_constraint, parameter) + ")");
				}
			}
			const String result = boundary_type(function->return_type_constraint, function);
			r_declaration += "\t" + String(function->is_static ? "static " : "") + result + " " + wrapper + "(" + String(", ").join(parameters) + ");\n";
			const String invoke = "m_" + symbol(entry.get_name()) + "(" + String(", ").join(arguments) + ")";
			r_definitions += result + " " + name + "::" + wrapper + "(" + String(", ").join(parameters) + ") {\n\t" + (result == "void" ? invoke : "return WGodotNative::convert<" + result + ">(WGodotNative::serialized_value(" + invoke + "))") + ";\n}\n\n";
			String method = "D_METHOD(" + quoted(entry.get_name());
			if (!names.is_empty()) {
				method += ", " + String(", ").join(names);
			}
			method += ")";
			String binding = "ClassDB::" + String(function->is_static ? "bind_static_method(get_class_static(), " : "bind_method(") + method + ", &" + name + "::" + wrapper + ")";
			if (!defaults.is_empty()) {
				const String pointer = "binding_" + symbol(entry.get_name());
				r_declaration += "private:\n\tstatic MethodBind *" + pointer + ";\npublic:\n";
				r_definitions += "MethodBind *" + name + "::" + pointer + " = nullptr;\n";
				binding = pointer + " = " + binding;
				default_bindings += "\t{\n\t\tVector<Variant> arguments;\n";
				for (const String &value : defaults) {
					default_bindings += "\t\targuments.push_back(" + value + ");\n";
				}
				default_bindings += "\t\t" + pointer + "->set_default_arguments(arguments);\n\t}\n";
			}
			bindings += "\t" + binding + ";\n";
		} else if (entry.type == Parser::ClassNode::Member::SIGNAL) {
			bindings += "\t{ MethodInfo signal(" + quoted(entry.get_name()) + ");\n";
			for (const auto *parameter : entry.signal->parameters) {
				if (native_only(parameter->type_constraint)) {
					unsupported(parameter, "reflection of a signal with native container/callback arguments");
				}
				bindings += "\t\t{ auto info = GetTypeInfo<" + boundary_type(parameter->type_constraint, parameter) + ">::get_class_info(); info.name = " + quoted(parameter->identifier->name) + "; signal.arguments.push_back(info); }\n";
			}
			bindings += "\t\tClassDB::add_signal(get_class_static(), signal);\n\t}\n";
		} else if (entry.type == Parser::ClassNode::Member::CONSTANT) {
			const Variant &value = entry.constant->initializer->reduced_value;
			if (value.get_type() == Variant::INT) {
				bindings += "\tClassDB::bind_integer_constant(get_class_static(), StringName(), " + quoted(entry.get_name()) + ", " + literal(value, member) + ");\n";
			}
		} else if (entry.type == Parser::ClassNode::Member::ENUM) {
			for (const auto &value : entry.m_enum->values) {
				bindings += "\tClassDB::bind_integer_constant(get_class_static(), " + quoted(entry.get_name()) + ", " + quoted(value.identifier->name) + ", " + itos(value.value) + ");\n";
			}
		}
	}
	if (!bindings.is_empty()) {
		r_declaration += "protected:\n\tstatic void _bind_methods();\npublic:\n";
		r_definitions += "void " + name + "::_bind_methods() {\n" + bindings + "}\n\n";
	}
	if (!default_bindings.is_empty()) {
		// Preload defaults may construct game resources. All classes and their
		// native factories must be registered before these values are evaluated.
		reflection_default_classes.insert(name);
		r_declaration += "\tstatic void bind_reflection_defaults();\n";
		r_definitions += "void " + name + "::bind_reflection_defaults() {\n" + default_bindings + "}\n\n";
	}
}
