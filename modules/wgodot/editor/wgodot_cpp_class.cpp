// wgodot-changes::file
#include "wgodot_cpp_async.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/inline_constants.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::function(const Parser::FunctionNode *p_function, String &r_declaration, const String &p_cpp_name) {
	function_failed = false;
	current_function = p_function;
	if (p_function->is_vararg()) {
		unsupported(p_function, "variadic functions");
		return String();
	}
	Vector<String> parameters;
	Vector<String> declarations;
	for (const auto *parameter : p_function->parameters) {
		String declaration = type(parameter->type_constraint, parameter) + " v_" + symbol(parameter->identifier->name);
		parameters.push_back(declaration);
		if (parameter->initializer && p_cpp_name.is_empty()) {
			// Default arguments are emitted in the header. Collect their helper
			// dependencies independently of calls already emitted in method bodies.
			HashSet<String> source_headers;
			SWAP(class_call_headers, source_headers);
			declaration += " = " + converted(parameter->initializer, parameter->type_constraint, parameter);
			for (const String &include : class_call_headers) {
				class_native_headers.insert(include);
			}
			SWAP(class_call_headers, source_headers);
		}
		declarations.push_back(declaration);
	}
	const bool initializer = !p_function->source_lambda && p_function->identifier && p_function->identifier->name == "_init";
	const bool is_static = p_function->source_lambda ? !p_function->source_lambda->use_self : p_function->is_static;
	const String return_type = initializer ? "void"
										   : function_result(p_function);
	const String name = p_cpp_name.is_empty() ? "m_" + symbol(p_function->identifier->name) : p_cpp_name;
	if (p_function->is_abstract) {
		r_declaration = "\tvirtual " + return_type + " " + name + "(" + String(", ").join(declarations) + ") = 0;\n";
		return String();
	}
	r_declaration = "\t" + String(is_static ? "static " : initializer || !p_cpp_name.is_empty() ? ""
																								: "virtual ") +
			return_type + " " + name + "(" + String(", ").join(declarations) + ");\n";
	if (p_function->is_coroutine) {
		WGodotCppAsync async(*this);
		return async.generate(p_function, name, parameters, r_declaration);
	}
	String body = suite(p_function->body, 1);
	if (return_type == "Variant" && !p_function->body->has_return) {
		body += "\treturn Variant();\n";
	}
	class_function_definitions.push_back({ return_type + " " + current_class->cpp_name + "::" + name + "(" + String(", ").join(parameters) + ")", body, is_static });
	return String();
}

void WGodotCppEmitter::emit_class(const WGodotCppProject::Class &p_class) {
	if (files.has(p_class.cpp_name + ".h") || class_lifecycles.has(p_class.node)) {
		return;
	}
	if (!required_classes.has(p_class.node) && WGodotGDScriptInlineConstants::can_omit_class(p_class.node)) {
		return;
	}
	// Lifecycle requirements are inherited from already emitted base classes.
	if (p_class.node->base_type.kind == Parser::DataType::CLASS) {
		if (const auto *parent = project.find_class(p_class.node->base_type.class_type)) {
			required_classes.insert(parent->node);
			emit_class(*parent);
		}
	}
	current_class = &p_class;
	temporary_index = 0;
	current_function = nullptr;
	function_failed = false;
	class_dependencies.clear();
	class_native_headers.clear();
	class_call_headers.clear();
	class_resource_types.clear();
	class_lambdas.clear();
	class_lambda_declarations.clear();
	class_lambda_definitions.clear();
	class_function_definitions.clear();
	class_uses_tasks = false;
	if (p_class.node->wgodot_is_interface) {
		emit_interface(p_class);
		return;
	}
	const Parser::ClassNode *node = p_class.node;
	const bool is_static = node->wgodot_static_class;
	const bool game_parent = node->base_type.kind == Parser::DataType::CLASS;
	const String parent = is_static ? "" : class_name(node->base_type, node);
	const String &name = p_class.cpp_name;
	String interface_bases;
	String interface_declarations;
	String interface_definitions;
	if (!is_static) {
		emit_interface_inheritance(p_class, interface_bases, interface_declarations, interface_definitions);
	}
	String declaration = "class " + name;
	// Native export includes the whole script hierarchy. Leaf classes cannot
	// acquire script subclasses at runtime, so their virtual calls can be devirtualized.
	if (!is_static && !node->is_abstract && !inherited_classes.has(node)) {
		declaration += " final";
	}
	String definitions;
	String fields;
	String property_reads;
	String property_writes;
	String property_list;
	String initialization;
	String static_fields;
	String static_initialization;
	if (!is_static) {
		declaration += " : public " + parent + interface_bases + " {\n\tGDCLASS(" + name + ", " + parent + ");\n\npublic:\n";
	} else {
		declaration += " {\npublic:\n";
	}
	declaration += interface_declarations;
	definitions += interface_definitions;
	for (const auto &entry : node->members) {
		function_failed = false;
		current_function = nullptr;
		switch (entry.type) {
			case Parser::ClassNode::Member::FUNCTION: {
				const auto *method = entry.function;
				const String method_name = method->identifier->name;
				const bool initializer = method_name == "_init";
				String signature;
				definitions += function(method, signature);
				declaration += signature;
				// An abstract initializer introduces a C++ virtual even though ordinary
				// constructors may otherwise change their signature between script levels.
				if (!method->is_static && game_parent && (!initializer || class_lifecycles[node->base_type.class_type].virtual_initializer)) {
					const auto *owner = member_owner(node->base_type.class_type, method_name);
					if (owner && owner->node->get_member(method_name).type == Parser::ClassNode::Member::FUNCTION) {
						const auto *inherited = owner->node->get_member(method_name).function;
						const String method_result = initializer ? "void" : function_result(method);
						const String inherited_result = initializer ? "void" : function_result(inherited);
						bool same_signature = method->parameters.size() == inherited->parameters.size() && method_result == inherited_result;
						for (uint32_t i = 0; same_signature && i < method->parameters.size(); i++) {
							const auto *parameter = method->parameters[i];
							const auto *base_parameter = inherited->parameters[i];
							same_signature = type(parameter->type_constraint, parameter) == type(base_parameter->type_constraint, base_parameter) && bool(parameter->initializer) == bool(base_parameter->initializer);
							if (same_signature && parameter->initializer) {
								same_signature = parameter->initializer->is_constant && base_parameter->initializer->is_constant && parameter->initializer->reduced_value == base_parameter->initializer->reduced_value;
							}
						}
						if (!same_signature) {
							unsupported(method, "method override with different signature or defaults: " + method_name);
						}
					}
				}
				break;
			}
			case Parser::ClassNode::Member::VARIABLE: {
				const auto *variable = entry.variable;
				if (variable->onready) {
					unsupported(variable, "onready field " + entry.get_name());
					break;
				}
				const auto datatype = variable_type(variable);
				const String field_type = type(datatype, variable);
				if (native_only(datatype) && variable->exported) {
					unsupported(variable, "exported native container/callback property " + entry.get_name() + "; scene serialization needs an explicit adapter");
				}
				const String field_name = "v_" + symbol(variable->identifier->name);
				if (variable->is_static) {
					static_fields += "\t\t" + field_type + " " + field_name + "{};\n";
				} else {
					fields += "\t" + field_type + " " + field_name + "{};\n";
				}
				if (variable->initializer) {
					const String value = converted(variable->initializer, datatype, variable);
					if (variable->is_static) {
						static_initialization += "\tfields." + field_name + " = " + value + ";\n";
					} else {
						initialization += "\t" + field_name + " = " + value + ";\n";
					}
				}
				const String getter = "read_" + symbol(variable->identifier->name);
				const String setter = "write_" + symbol(variable->identifier->name);
				const StringName property_getter = accessor_name(variable, false);
				const StringName property_setter = accessor_name(variable, true);
				const String getter_const = !variable->is_static && property_getter.is_empty() ? " const" : "";
				const String static_modifier = variable->is_static ? "static " : "";
				const String storage = String(variable->is_static ? "static_fields()." : "") + field_name;
				// Only serialization and interface forwarding use these wrappers.
				// Ordinary property access calls script accessors or uses storage directly.
				const bool serialized = !is_static && variable->exported && !native_only(datatype);
				if (serialized || interface_property_getters.has(variable)) {
					declaration += "\t" + static_modifier + field_type + " " + getter + "()" + getter_const + ";\n";
					definitions += field_type + " " + name + "::" + getter + "()" + getter_const + " { return " + (property_getter.is_empty() ? storage : "m_" + symbol(property_getter) + "()") + "; }\n";
				}
				if (serialized || interface_property_setters.has(variable)) {
					declaration += "\t" + static_modifier + "void " + setter + "(" + field_type + " p_value);\n";
					definitions += "void " + name + "::" + setter + "(" + field_type + " p_value) { " + (property_setter.is_empty() ? storage + " = p_value" : "m_" + symbol(property_setter) + "(p_value)") + "; }\n";
				}
				if (serialized) {
					const String property = quoted(variable->identifier->name);
					property_reads += "\tif (p_name == " + property + ") { r_value = const_cast<" + name + " *>(this)->" + getter + "(); return true; }\n";
					property_writes += "\tif (p_name == " + property + ") { " + setter + "(WGodotNative::convert<" + field_type + ">(p_value)); return true; }\n";
					property_list += "\t{ PropertyInfo info = GetTypeInfo<" + field_type + ">::get_class_info(); info.name = " + property + "; info.usage = " + String(variable->is_static ? "PROPERTY_USAGE_NONE" : "PROPERTY_USAGE_STORAGE") + "; p_list->push_back(info); }\n";
				}
				if (variable->property == Parser::VariableNode::PROP_INLINE) {
					for (const auto *accessor : { variable->getter, variable->setter }) {
						if (accessor) {
							String signature;
							definitions += function(accessor, signature);
							declaration += signature;
						}
					}
				}
				break;
			}
			case Parser::ClassNode::Member::SIGNAL: {
				const auto *signal = entry.signal;
				const String signal_type = signature_type(signal, true).replace("WSignal<", "SignalSource<");
				declaration += "\t" + signal_type + " s_" + symbol(signal->identifier->name) + "{this};\n";
				break;
			}
			case Parser::ClassNode::Member::CONSTANT: {
				// Even an unused preload belongs to the class's retained resources.
				const auto *initializer = entry.constant->initializer;
				if (initializer->reduced && !initializer->type_constraint.is_meta_type && initializer->reduced_value.get_type() == Variant::OBJECT) {
					(void)literal(initializer->reduced_value, initializer);
				}
				break;
			}
			case Parser::ClassNode::Member::CLASS:
			case Parser::ClassNode::Member::GROUP:
			case Parser::ClassNode::Member::ENUM:
			case Parser::ClassNode::Member::ENUM_VALUE:
				break;
			default:
				unsupported(entry.get_source_node(), "class member " + entry.get_name());
				break;
		}
	}
	if (!is_static && !property_reads.is_empty()) {
		declaration += "protected:\n\tbool _get(const StringName &p_name, Variant &r_value) const;\n\tbool _set(const StringName &p_name, const Variant &p_value);\n\tvoid _get_property_list(List<PropertyInfo> *p_list) const;\npublic:\n";
		definitions += "bool " + name + "::_get(const StringName &p_name, Variant &r_value) const {\n" + property_reads + "\treturn false;\n}\n\n";
		definitions += "bool " + name + "::_set(const StringName &p_name, const Variant &p_value) {\n" + property_writes + "\treturn false;\n}\n\n";
		definitions += "void " + name + "::_get_property_list(List<PropertyInfo> *p_list) const {\n" + property_list + "}\n\n";
	}
	emit_container_constants(p_class, declaration, definitions);
	emit_class_lifecycle(p_class, initialization, static_fields, static_initialization, declaration, definitions);
	if (!is_static) {
		emit_virtuals(p_class, declaration, definitions);
	}
	definitions += class_lambda_definitions;
	for (const FunctionDefinition &function : class_function_definitions) {
		definitions += function.signature + " {\n";
		if (function.prepare_class && class_lifecycles[node].prepare) {
			definitions += "\tprepare_game_class();\n";
		}
		definitions += function.body + "}\n\n";
	}
	declaration += fields + class_lambda_declarations + "};\n";
	const String origin = source_header(p_class.script_path, node->fqcn);
	String header = origin + "#pragma once\n#include \"game_types.h\"\n";
	Vector<String> includes;
	for (const String &include : class_native_headers) {
		includes.push_back(include);
	}
	includes.sort();
	for (const String &include : includes) {
		header += "#include \"" + include + "\"\n";
	}
	if (game_parent) {
		header += "#include \"" + parent + ".h\"\n";
	}
	header += "\nnamespace WGodotGame {\n";
	Vector<String> dependencies;
	for (const String &dependency : class_dependencies) {
		dependencies.push_back(dependency);
	}
	dependencies.sort();
	for (const String &dependency : dependencies) {
		if (dependency != name && dependency != parent) {
			header += "class " + dependency + ";\n";
		}
	}
	header += declaration + "} // namespace WGodotGame\n";
	String source = origin + "#include \"" + name + ".h\"\n";
	Vector<String> call_headers;
	for (const String &include : class_call_headers) {
		call_headers.push_back(include);
	}
	call_headers.sort();
	for (const String &include : call_headers) {
		source += "#include \"" + include + "\"\n";
	}
	for (const String &dependency : dependencies) {
		if (dependency != name && dependency != parent) {
			source += "#include \"" + dependency + ".h\"\n";
		}
	}
	source += "\nnamespace WGodotGame {\n" + definitions + "} // namespace WGodotGame\n";
	files.insert(name + ".h", header);
	files.insert(name + ".cpp", source);
}

void WGodotCppEmitter::register_class(const WGodotCppProject::Class &p_class, HashSet<String> &r_registered, String &r_code) {
	if (r_registered.has(p_class.cpp_name) || p_class.node->wgodot_static_class || p_class.node->wgodot_is_interface || !files.has(p_class.cpp_name + ".h")) {
		return;
	}
	if (p_class.node->base_type.kind == Parser::DataType::CLASS) {
		const auto *parent = project.find_class(p_class.node->base_type.class_type);
		if (parent) {
			register_class(*parent, r_registered, r_code);
		}
	}
	r_registered.insert(p_class.cpp_name);
	r_code += String(p_class.node->is_abstract ? "\tGDREGISTER_ABSTRACT_CLASS(" : "\tGDREGISTER_CLASS(") + p_class.cpp_name + ");\n";
}
