// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"
#include "core/object/wgodot_native_interfaces.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/wgodot_gd/interface_helpers.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

bool WGodotCppEmitter::is_interface_type(const Parser::DataType &p_type) const {
	return (p_type.kind == Parser::DataType::CLASS && p_type.class_type->wgodot_is_interface) ||
			(p_type.kind == Parser::DataType::NATIVE && WGodotNativeInterfaces::get_descriptor(p_type.native_type));
}

String WGodotCppEmitter::native_interface_name(const StringName &p_name) const {
	return "NativeInterface_" + String(p_name).sha256_text().substr(0, 16);
}

String WGodotCppEmitter::interface_value_definition(const String &p_name, const String &p_base, const String &p_interface, const String &p_members) const {
	const String base = "WGodotNative::InterfaceValue<" + p_name + ", " + p_base + ", " + p_interface + ">";
	return "class " + p_name + " : public " + base + " {\npublic:\n\tusing " + base + "::InterfaceValue;\n\tstatic const StringName &get_class_static() { static const StringName name = \"" + p_name + "\"; return name; }\n" + p_members + "};\n";
}

String WGodotCppEmitter::interface_value_traits(const String &p_name) const {
	const String qualified = "WGodotGame::" + p_name;
	String code = "template <> struct GetTypeInfo<" + qualified + "> : WGodotNative::InterfaceTypeInfo<" + qualified + "> {};\n";
	code += "template <> struct PtrToArg<" + qualified + "> : WGodotNative::InterfacePtrToArg<" + qualified + "> {};\n";
	code += "template <> struct VariantInternalAccessor<" + qualified + "> : WGodotNative::InterfaceVariantAccessor<" + qualified + "> {};\n";
	code += "template <> struct VariantObjectClassChecker<" + qualified + "> { static bool check(const Variant &p_value) { return " + qualified + "::accepts(p_value); } };\n";
	code += "namespace GodotTypeInfo::Internal {\ntemplate <> inline const StringName &get_object_class_name_or_empty<" + qualified + ">() { return " + qualified + "::get_class_static(); }\n";
	code += "template <> inline const String get_variant_type_identifier<" + qualified + ">() { return " + qualified + "::get_class_static(); }\n}\n";
	return code;
}

void WGodotCppEmitter::emit_native_interface(const WGodotNativeInterfaces::Descriptor &p_interface) {
	const String name = native_interface_name(p_interface.name);
	class_native_headers.clear();
	class_dependencies.clear();
	String signals;
	String signal_declarations;
	String signal_definitions;
	if (!p_interface.signals.is_empty()) {
		const String facet = name + "_Signals";
		signals = "class " + facet + " {\n\tWGD_INTERFACE(" + facet + ");\npublic:\n";
		for (const auto &entry : p_interface.signals) {
			const String signal_type = native_interface_signal_type(entry.value);
			const String getter = "signal_" + symbol(entry.key);
			signals += "\tvirtual " + signal_type + " " + getter + "() = 0;\n";
			signal_declarations += "\t" + signal_type + " " + getter + "() const;\n";
			signal_definitions += "inline " + signal_type + " " + name + "::" + getter + "() const {\n\tif (!is_valid()) { return {}; }\n\tif (auto *signals = static_cast<" + facet + " *>(ptr()->wgodot_get_native_interface(&" + facet + "::wgodot_interface_tag))) { return signals->" + getter + "(); }\n\treturn " + signal_type + "(Signal(ptr(), SNAME(" + quoted(entry.key) + ")));\n}\n";
		}
		signals += "};\n";
	}
	String header = source_header(p_interface.cpp_header, p_interface.name) + "#pragma once\n#include \"modules/wgodot/native/wgodot_native_interface.h\"\n#include \"" + p_interface.cpp_header + "\"\n#include \"" + native_headers[p_interface.native_base] + "\"\n\nnamespace WGodotGame {\n";
	String includes;
	for (const String &include : class_native_headers) {
		if (include != name + ".h") {
			includes += "#include \"" + include + "\"\n";
		}
	}
	header = header.replace("namespace WGodotGame {", includes + "namespace WGodotGame {\nclass " + name + ";");
	String value = interface_value_definition(name, native_cpp_names[p_interface.native_base], p_interface.cpp_type);
	value = value.replace("};\n", signal_declarations + "};\n");
	header += value + signals + signal_definitions;
	header += "}\n" + interface_value_traits(name);
	files.insert(name + ".h", header);
}

String WGodotCppEmitter::native_interface_signal_type(const MethodInfo &p_signal) {
	GDScriptAnalyzer analyzer(project.find_parser(current_class->script_path));
	Vector<String> arguments;
	for (const PropertyInfo &argument : p_signal.arguments) {
		arguments.push_back(type(analyzer.type_from_property(argument, true, current_class->node), current_class->node));
	}
	class_native_headers.insert("modules/wgodot/native/wgodot_native_signal.h");
	return "WGodotNative::WSignal<" + String(", ").join(arguments) + ">";
}

void WGodotCppEmitter::emit_interface(const WGodotCppProject::Class &p_class) {
	const StringName native = native_base(p_class.node->self_type);
	const String *native_header = native_headers.getptr(native);
	if (!native_header) {
		unsupported(p_class.node, "native interface base " + String(native));
		return;
	}
	const String &name = p_class.cpp_name;
	const String interface = interface_cpp_type(p_class.node);
	String contract = "class " + interface + " {\n\tWGD_INTERFACE(" + interface + ");\npublic:\n";
	HashSet<StringName> declared;
	for (const auto &member : p_class.node->members) {
		const StringName member_name = member.get_name();
		declared.insert(member_name);
		if (member.type == Parser::ClassNode::Member::FUNCTION) {
			Vector<String> parameters;
			for (const auto *parameter : member.function->parameters) {
				parameters.push_back(type(parameter->type_constraint, parameter));
			}
			contract += "\tvirtual " + function_result(member.function) + " " + String(member_name) + "(" + String(", ").join(parameters) + ") = 0;\n";
		} else if (member.type == Parser::ClassNode::Member::VARIABLE) {
			const String value_type = type(member.variable->type_constraint, member.variable);
			contract += "\tvirtual " + value_type + " get_" + symbol(member_name) + "() = 0;\n";
			if (!member.variable->wgodot_readonly) {
				contract += "\tvirtual void set_" + symbol(member_name) + "(" + value_type + ") = 0;\n";
			}
		} else if (member.type == Parser::ClassNode::Member::SIGNAL) {
			contract += "\tvirtual " + signature_type(member.signal, true) + " signal_" + symbol(member_name) + "() = 0;\n";
		}
	}
	// Implementors inherit each resolved contract. Flattening declarations
	// avoids diamonds while preserving every interface identity.
	for (const StringName &native_name : p_class.node->wgodot_native_interfaces) {
		const auto *parent = WGodotNativeInterfaces::get_descriptor(native_name);
		used_native_interfaces.insert(native_name);
		class_native_headers.insert(parent->cpp_header);
		for (const auto &entry : parent->methods) {
			if (declared.has(entry.key)) {
				continue;
			}
			declared.insert(entry.key);
			const String traits = "Method_" + symbol(entry.key);
			contract += "\tusing " + traits + " = WGodotNative::InterfaceMethod<decltype(&" + parent->cpp_type + "::" + String(entry.key) + ")>;\n";
			Vector<String> parameters;
			for (int i = 0; i < entry.value.arguments.size(); i++) {
				parameters.push_back(traits + "::Argument<" + itos(i) + ">");
			}
			contract += "\tvirtual " + traits + "::Result " + String(entry.key) + "(" + String(", ").join(parameters) + ")" + String(entry.value.flags & METHOD_FLAG_CONST ? " const" : "") + " = 0;\n";
		}
	}
	contract += "};\n";
	String constant_declarations;
	String constant_definitions;
	emit_container_constants(p_class, constant_declarations, constant_definitions);
	String header = source_header(p_class.script_path, p_class.node->fqcn) + "#pragma once\n#include \"game_types.h\"\n#include \"modules/wgodot/native/wgodot_native_interface.h\"\n#include \"" + *native_header + "\"\n";
	Vector<String> headers;
	for (const String &include : class_native_headers) {
		if (include != name + ".h") {
			headers.push_back(include);
		}
	}
	headers.sort();
	for (const String &include : headers) {
		header += "#include \"" + include + "\"\n";
	}
	header += "\nnamespace WGodotGame {\n";
	Vector<String> dependencies;
	for (const String &dependency : class_dependencies) {
		dependencies.push_back(dependency);
	}
	dependencies.sort();
	for (const String &dependency : dependencies) {
		header += "class " + dependency + ";\n";
	}
	header += contract + interface_value_definition(name, native_cpp_names[native], interface, constant_declarations);
	header += "}\n" + interface_value_traits(name);
	files.insert(name + ".h", header);
	if (!constant_definitions.is_empty()) {
		String source = source_header(p_class.script_path, p_class.node->fqcn) + "#include \"" + name + ".h\"\n";
		headers.clear();
		for (const String &include : class_call_headers) {
			headers.push_back(include);
		}
		headers.sort();
		for (const String &include : headers) {
			source += "#include \"" + include + "\"\n";
		}
		for (const String &dependency : dependencies) {
			if (dependency != name) {
				source += "#include \"" + dependency + ".h\"\n";
			}
		}
		source += "\nnamespace WGodotGame {\n" + constant_definitions + "}\n";
		files.insert(name + ".cpp", source);
	}
}

String WGodotCppEmitter::register_interfaces() {
	String contracts;
	String implementations;
	LocalVector<StringName> native_classes;
	ClassDB::get_class_list(native_classes);
	Vector<StringName> native_interfaces;
	for (const StringName &name : used_native_interfaces) {
		native_interfaces.push_back(name);
	}
	native_interfaces.sort();
	for (const StringName &name : native_interfaces) {
		const auto *contract = WGodotNativeInterfaces::get_descriptor(name);
		const String id = native_interface_name(name);
		Vector<String> parents;
		for (const StringName &parent : contract->parents) {
			parents.push_back("SNAME(" + quoted(native_interface_name(parent)) + ")");
		}
		contracts += "\tWGodotNativeInterfaces::add_contract(SNAME(" + quoted(id) + "), SNAME(" + quoted(contract->native_base) + "), {" + String(", ").join(parents) + "});\n";
		for (const StringName &native_class : native_classes) {
			if (WGodotNativeInterfaces::accepts(native_class, name)) {
				implementations += "\tWGodotNativeInterfaces::add_implementation(SNAME(" + quoted(id) + "), SNAME(" + quoted(native_class) + "));\n";
			}
		}
		for (const auto &candidate : project.get_classes()) {
			if (!candidate.node->wgodot_is_interface && !candidate.node->wgodot_static_class && WGodotGDScriptInterfaceHelpers::class_implements_native_interface(candidate.node, name)) {
				implementations += "\tWGodotNativeInterfaces::add_implementation(SNAME(" + quoted(id) + "), SNAME(" + quoted(candidate.cpp_name) + "));\n";
			}
		}
	}
	for (const auto &entry : project.get_classes()) {
		if (!entry.node->wgodot_is_interface) {
			continue;
		}
		Vector<String> parents;
		for (const auto *parent : entry.node->wgodot_resolved_interfaces) {
			const auto *description = project.find_class(parent);
			if (description) {
				parents.push_back("SNAME(" + quoted(description->cpp_name) + ")");
			}
		}
		for (const StringName &parent : entry.node->wgodot_native_interfaces) {
			parents.push_back("SNAME(" + quoted(native_interface_name(parent)) + ")");
		}
		contracts += "\tWGodotNativeInterfaces::add_contract(SNAME(" + quoted(entry.cpp_name) + "), SNAME(" + quoted(native_base(entry.node->self_type)) + "), {" + String(", ").join(parents) + "});\n";
		for (const auto &candidate : project.get_classes()) {
			if (!candidate.node->wgodot_is_interface && !candidate.node->wgodot_static_class && WGodotGDScriptInterfaceHelpers::class_implements_interface_type(candidate.node, entry.node)) {
				implementations += "\tWGodotNativeInterfaces::add_implementation(SNAME(" + quoted(entry.cpp_name) + "), SNAME(" + quoted(candidate.cpp_name) + "));\n";
			}
		}
	}
	return contracts + implementations;
}
