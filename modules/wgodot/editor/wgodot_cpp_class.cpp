// wgodot-changes::file
#include "wgodot_cpp_async.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::function(const Parser::FunctionNode *p_function, String &r_declaration, const String &p_cpp_name) {
	function_failed = false;
	current_function = p_function;
	if (p_function->is_vararg() || p_function->is_abstract) {
		unsupported(p_function, p_function->is_vararg() ? "variadic functions" : "abstract methods");
		return String();
	}
	Vector<String> parameters;
	Vector<String> declarations;
	for (const auto *parameter : p_function->parameters) {
		String declaration = type(parameter->type_constraint, parameter) + " v_" + symbol(parameter->identifier->name);
		parameters.push_back(declaration);
		if (parameter->initializer && p_cpp_name.is_empty()) {
			declaration += " = " + converted(parameter->initializer, parameter->type_constraint);
		}
		declarations.push_back(declaration);
	}
	const bool initializer = !p_function->source_lambda && p_function->identifier && p_function->identifier->name == "_init";
	const bool is_static = p_function->source_lambda ? !p_function->source_lambda->use_self : p_function->is_static;
	const String return_type = p_function->is_coroutine ? "Variant" : initializer ? "void"
																				  : type(p_function->return_type_constraint, p_function);
	const String name = p_cpp_name.is_empty() ? "m_" + symbol(p_function->identifier->name) : p_cpp_name;
	r_declaration = "\t" + String(is_static ? "static " : initializer || !p_cpp_name.is_empty() ? ""
																								: "virtual ") +
			return_type + " " + name + "(" + String(", ").join(declarations) + ");\n";
	if (p_function->is_coroutine) {
		WGodotCppAsync async(*this);
		return async.generate(p_function, name, parameters, r_declaration);
	}
	String body = is_static ? "\tprepare_game_class();\n" : "";
	body += suite(p_function->body, 1);
	if (return_type == "Variant" && !p_function->body->has_return) {
		body += "\treturn Variant();\n";
	}
	return return_type + " " + current_class->cpp_name + "::" + name + "(" + String(", ").join(parameters) + ") {\n" + body + "}\n\n";
}

void WGodotCppEmitter::emit_class(const WGodotCppProject::Class &p_class) {
	current_class = &p_class;
	current_function = nullptr;
	function_failed = false;
	if (p_class.node->wgodot_is_interface) {
		emit_interface(p_class);
		return;
	}
	class_dependencies.clear();
	class_native_headers.clear();
	class_call_headers.clear();
	class_resource_types.clear();
	class_lambdas.clear();
	class_lambda_declarations.clear();
	class_lambda_definitions.clear();
	const Parser::ClassNode *node = p_class.node;
	const bool is_static = node->wgodot_static_class;
	const bool game_parent = node->base_type.kind == Parser::DataType::CLASS;
	const String parent = is_static ? "" : class_name(node->base_type, node);
	const String &name = p_class.cpp_name;
	if (node->is_abstract) {
		unsupported(node, "abstract class registration");
	}
	String declaration = "class " + name;
	String definitions;
	String fields;
	String bindings;
	String initialization;
	String static_fields;
	String static_initialization;
	if (!is_static) {
		declaration += " : public " + parent + " {\n\tGDCLASS(" + name + ", " + parent + ");\n\nprotected:\n\tstatic void _bind_methods();\n";
		declaration += "\tvirtual void initialize_fields()" + String(game_parent ? " override" : "") + ";\n";
		declaration += "\tvirtual void initialize_default()" + String(game_parent ? " override" : "") + ";\n";
		if (!game_parent) {
			declaration += "\tWGodotNative::Construction construction_mode;\n\tbool game_initialized = false;\n";
			class_native_headers.insert("modules/wgodot/native/wgodot_native_task.h");
			declaration += "\tWGodotNative::TaskOwner game_tasks;\n";
		}
		declaration += "\npublic:\n\texplicit " + name + "(WGodotNative::Construction p_mode = WGodotNative::Construction::SCENE);\n\t~" + name + "() override;\n";
		definitions += name + "::" + name + "(WGodotNative::Construction p_mode) : " + (game_parent ? parent + "(p_mode)" : "construction_mode(p_mode)") + " {}\n";
		definitions += name + "::~" + name + "() = default;\n\n";
		if (game_parent) {
			initialization = "\t" + parent + "::initialize_fields();\n";
		}
	} else {
		declaration += " {\npublic:\n";
	}
	declaration += "\tstatic void prepare_game_class();\n";
	for (const auto &entry : node->members) {
		function_failed = false;
		current_function = nullptr;
		switch (entry.type) {
			case Parser::ClassNode::Member::FUNCTION: {
				const auto *method = entry.function;
				const String method_name = method->identifier->name;
				String signature;
				definitions += function(method, signature);
				declaration += signature;
				bool inherits_binding = false;
				if (!method->is_static && method_name != "_init" && game_parent) {
					const auto *owner = member_owner(node->base_type.class_type, method_name);
					if (owner && owner->node->get_member(method_name).type == Parser::ClassNode::Member::FUNCTION) {
						const auto *inherited = owner->node->get_member(method_name).function;
						inherits_binding = true;
						const String method_result = method->is_coroutine ? "Variant" : type(method->return_type_constraint, method);
						const String inherited_result = inherited->is_coroutine ? "Variant" : type(inherited->return_type_constraint, inherited);
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
				// The inherited MethodBind calls a virtual C++ method, so it already
				// dispatches to this implementation and must not be registered twice.
				if (!is_static && !method->is_static && method_name != "_init" && !inherits_binding) {
					String arguments;
					String defaults;
					for (const auto *parameter : method->parameters) {
						arguments += ", " + quoted(parameter->identifier->name);
						if (parameter->initializer) {
							defaults += ", DEFVAL(" + expression(parameter->initializer) + ")";
						}
					}
					bindings += "\tClassDB::bind_method(D_METHOD(" + quoted(method_name) + arguments + "), &" + name + "::m_" + symbol(method_name) + defaults + ");\n";
				}
				break;
			}
			case Parser::ClassNode::Member::VARIABLE: {
				const auto *variable = entry.variable;
				if (variable->onready) {
					unsupported(variable, "onready field " + entry.get_name());
					break;
				}
				const String field_type = type(variable->type_constraint, variable);
				const String field_name = "v_" + symbol(variable->identifier->name);
				if (variable->is_static) {
					static_fields += "\t\t" + field_type + " " + field_name + "{};\n";
				} else {
					fields += "\t" + field_type + " " + field_name + "{};\n";
				}
				if (variable->initializer) {
					const String value = converted(variable->initializer, variable->type_constraint);
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
				declaration += "\t" + static_modifier + field_type + " " + getter + "()" + getter_const + ";\n\t" + static_modifier + "void " + setter + "(" + field_type + " p_value);\n";
				definitions += field_type + " " + name + "::" + getter + "()" + getter_const + " { return " + (property_getter.is_empty() ? storage : "m_" + symbol(property_getter) + "()") + "; }\n";
				definitions += "void " + name + "::" + setter + "(" + field_type + " p_value) { " + (property_setter.is_empty() ? storage + " = p_value" : "m_" + symbol(property_setter) + "(p_value)") + "; }\n";
				if (!is_static) {
					const String bind_getter = variable->is_static ? "instance_" + getter : getter;
					const String bind_setter = variable->is_static ? "instance_" + setter : setter;
					if (variable->is_static) {
						declaration += "\t" + field_type + " " + bind_getter + "() { return " + getter + "(); }\n\tvoid " + bind_setter + "(" + field_type + " p_value) { " + setter + "(p_value); }\n";
					}
					bindings += "\tClassDB::bind_method(D_METHOD(" + quoted(bind_getter) + "), &" + name + "::" + bind_getter + ");\n";
					bindings += "\tClassDB::bind_method(D_METHOD(" + quoted(bind_setter) + ", \"value\"), &" + name + "::" + bind_setter + ");\n";
					bindings += "\t{\n\t\tPropertyInfo info = GetTypeInfo<" + field_type + ">::get_class_info();\n\t\tinfo.name = " + quoted(variable->identifier->name) + ";\n\t\tinfo.usage = " + String(variable->is_static ? "PROPERTY_USAGE_NONE" : "PROPERTY_USAGE_STORAGE") + ";\n\t\tADD_PROPERTY(info, " + quoted(bind_setter) + ", " + quoted(bind_getter) + ");\n\t}\n";
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
				bindings += "\t{\n\t\tMethodInfo info(" + quoted(signal->identifier->name) + ");\n";
				for (const auto *parameter : signal->parameters) {
					bindings += "\t\t{ PropertyInfo argument = GetTypeInfo<" + type(parameter->type_constraint, parameter) + ">::get_class_info(); argument.name = " + quoted(parameter->identifier->name) + "; info.arguments.push_back(argument); }\n";
				}
				bindings += "\t\tADD_SIGNAL(info);\n\t}\n";
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
	if (!is_static) {
		emit_virtuals(p_class, declaration, definitions);
		definitions += "void " + name + "::_bind_methods() {\n" + bindings + "}\n\n";
		definitions += "void " + name + "::initialize_fields() {\n\tprepare_game_class();\n" + initialization + "}\n\n";
		const auto *owner = member_owner(node, "_init");
		const auto *initializer = owner ? owner->node->get_member("_init").function : nullptr;
		Vector<String> parameters;
		Vector<String> factory_parameters;
		Vector<String> arguments;
		bool default_constructible = true;
		if (initializer) {
			for (const auto *parameter : initializer->parameters) {
				String parameter_text = type(parameter->type_constraint, parameter) + " v_" + symbol(parameter->identifier->name);
				parameters.push_back(parameter_text);
				arguments.push_back("v_" + symbol(parameter->identifier->name));
				if (parameter->initializer) {
					parameter_text += " = " + converted(parameter->initializer, parameter->type_constraint);
				} else {
					default_constructible = false;
				}
				factory_parameters.push_back(parameter_text);
			}
		}
		definitions += "void " + name + "::initialize_default() {\n";
		if (initializer && default_constructible) {
			definitions += "\tm_" + symbol("_init") + "();\n";
		} else if (!default_constructible) {
			definitions += "\tERR_FAIL_MSG(\"This native class requires constructor arguments.\");\n";
		}
		definitions += "}\n\n";
		const String instance_type = type(node->self_type, node);
		declaration += "\tstatic " + instance_type + " create(" + String(", ").join(factory_parameters) + ");\n";
		definitions += instance_type + " " + name + "::create(" + String(", ").join(parameters) + ") {\n\tprepare_game_class();\n\tauto instance = memnew(" + name + "(WGodotNative::Construction::EXPLICIT));\n";
		if (initializer) {
			definitions += "\tinstance->m_" + symbol("_init") + "(" + String(", ").join(arguments) + ");\n";
		}
		definitions += "\treturn instance;\n}\n\n";
	}
	String prepare = game_parent ? "\t" + parent + "::prepare_game_class();\n" : "";
	if (!class_resource_types.is_empty()) {
		class_call_headers.insert("core/io/resource_loader.h");
		Vector<String> paths;
		for (const auto &resource : class_resource_types) {
			paths.push_back(resource.key);
		}
		paths.sort();
		String resources;
		for (const String &path : paths) {
			const String field = "resource_" + path.sha256_text().substr(0, 16);
			static_fields += "\t\t" + class_resource_types[path] + " " + field + ";\n";
			resources += "\tfields." + field + " = ::ResourceLoader::load(String::utf8(" + quoted(path) + "));\n";
			resources += "\tERR_FAIL_COND_MSG(fields." + field + ".is_null(), \"Could not preload native game resource: \" + String::utf8(" + quoted(path) + "));\n";
		}
		static_initialization = resources + static_initialization;
	}
	const bool has_static_initializer = node->has_function(SNAME("_static_init"));
	if (!static_fields.is_empty() || has_static_initializer) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_static.h");
		declaration += "\tstruct StaticFields {\n" + static_fields + "\t};\n\tstatic StaticFields &static_fields();\nprivate:\n\tstatic void initialize_static_fields(StaticFields &fields);\npublic:\n";
		definitions += name + "::StaticFields &" + name + "::static_fields() {\n\treturn WGodotNative::StaticStorage<StaticFields>::get(&initialize_static_fields);\n}\n\n";
		if (has_static_initializer) {
			static_initialization += "\tm_" + symbol(SNAME("_static_init")) + "();\n";
		}
		definitions += "void " + name + "::initialize_static_fields(StaticFields &fields) {\n" + prepare + static_initialization + "}\n\n";
		prepare = "\t(void)static_fields();\n";
	}
	definitions += "void " + name + "::prepare_game_class() {\n" + prepare + "}\n\n";
	declaration += fields + class_lambda_declarations + "};\n";
	definitions += class_lambda_definitions;
	String header = "// wgodot-changes::file\n// Generated by WGodot.\n#pragma once\n#include \"game_types.h\"\n";
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
	String source = "// wgodot-changes::file\n// Generated by WGodot.\n#include \"" + name + ".h\"\n";
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
	if (r_registered.has(p_class.cpp_name) || p_class.node->wgodot_static_class || p_class.node->wgodot_is_interface) {
		return;
	}
	if (p_class.node->base_type.kind == Parser::DataType::CLASS) {
		const auto *parent = project.find_class(p_class.node->base_type.class_type);
		if (parent) {
			register_class(*parent, r_registered, r_code);
		}
	}
	r_registered.insert(p_class.cpp_name);
	r_code += "\tGDREGISTER_CLASS(" + p_class.cpp_name + ");\n";
}
