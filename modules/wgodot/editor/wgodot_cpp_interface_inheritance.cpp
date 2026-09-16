// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"
#include "modules/gdscript/wgodot_stdlib.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::interface_cpp_type(const Parser::ClassNode *p_interface) {
	const auto *description = project.find_class(p_interface);
	if (const auto *native = WGodotGDScriptStdLib::get_native_interface(p_interface->wgodot_interface_name)) {
		class_native_headers.insert(native->cpp_header);
		return native->cpp_type;
	}
	class_native_headers.insert(description->cpp_name + ".h");
	return description->cpp_name + "_Interface";
}

StringName WGodotCppEmitter::interface_native_metadata(const Parser::ClassNode *p_interface) const {
	const auto *native = WGodotGDScriptStdLib::get_native_interface(p_interface->wgodot_interface_name);
	return native ? native->api_class : StringName();
}

void WGodotCppEmitter::emit_interface_inheritance(const WGodotCppProject::Class &p_class, String &r_bases, String &r_declaration, String &r_definitions) {
	String query;
	HashSet<StringName> forwarded;
	for (const auto &contract : project.get_classes()) {
		if (!contract.node->wgodot_is_interface || !WGodotGDScriptInterfaceHelpers::class_implements_interface_type(p_class.node, contract.node)) {
			continue;
		}
		const String interface = interface_cpp_type(contract.node);
		const auto &base = p_class.node->base_type;
		const bool inherited = base.kind == Parser::DataType::CLASS ? WGodotGDScriptInterfaceHelpers::class_implements_interface_type(base.class_type, contract.node) : ClassDB::wgodot_class_implements_interface(base.native_type, WGodotGDScriptInterfaceHelpers::get_interface_id(contract.node));
		if (!inherited) {
			r_bases += ", public " + interface;
			query += "\tif (p_type == &" + interface + "::wgodot_interface_tag) { return static_cast<" + interface + " *>(this); }\n";
		}
		const StringName metadata = interface_native_metadata(contract.node);
		for (const auto &entry : contract.node->members) {
			if (entry.type != Parser::ClassNode::Member::FUNCTION || forwarded.has(entry.get_name())) {
				continue;
			}
			const auto *owner = member_owner(p_class.node, entry.get_name());
			// Native bases already implement their interface; only a script override
			// needs a bridge to its generated method.
			if (inherited && (!owner || owner->node != p_class.node)) {
				continue;
			}
			if (!owner || owner->node->get_member(entry.get_name()).type != Parser::ClassNode::Member::FUNCTION) {
				unsupported(p_class.node, "C++ interface implementation for " + String(entry.get_name()));
				continue;
			}
			const auto *method = owner->node->get_member(entry.get_name()).function;
			if (method->is_coroutine != entry.function->is_coroutine) {
				unsupported(method, "interface implementation with a different coroutine result type");
				continue;
			}
			forwarded.insert(entry.get_name());
			class_native_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
			const String traits = "Interface_" + symbol(entry.get_name());
			r_declaration += "\tusing " + traits + " = WGodotNative::InterfaceMethod<decltype(&" + interface + "::" + String(entry.get_name()) + ")>;\n";
			Vector<String> parameters;
			Vector<String> arguments;
			for (uint32_t i = 0; i < method->parameters.size(); i++) {
				const String argument = "p_argument_" + itos(i);
				parameters.push_back(traits + "::Argument<" + itos(i) + "> " + argument);
				arguments.push_back("WGodotNative::convert<" + type(method->parameters[i]->type_constraint, method->parameters[i]) + ">(" + argument + ")");
			}
			const MethodBind *native_method = metadata.is_empty() ? nullptr : ClassDB::get_method(metadata, entry.get_name());
			const bool constant = native_method && native_method->is_const();
			const String suffix = constant ? " const" : "";
			r_declaration += "\t" + traits + "::Result " + String(entry.get_name()) + "(" + String(", ").join(parameters) + ")" + suffix + " override;\n";
			String invoke = String(constant ? "const_cast<" + p_class.cpp_name + " *>(this)->" : "this->") + "m_" + symbol(entry.get_name()) + "(" + String(", ").join(arguments) + ")";
			if (function_result(method) != "void") {
				invoke = "WGodotNative::convert<" + traits + "::Result>(" + invoke + ")";
			}
			r_definitions += p_class.cpp_name + "::" + traits + "::Result " + p_class.cpp_name + "::" + String(entry.get_name()) + "(" + String(", ").join(parameters) + ")" + suffix + " { return " + invoke + "; }\n\n";
		}
	}
	if (!query.is_empty()) {
		r_declaration += "\tvoid *wgodot_get_native_interface(const void *p_type) override;\n";
		r_definitions += "void *" + p_class.cpp_name + "::wgodot_get_native_interface(const void *p_type) {\n" + query + "\treturn " + class_name(p_class.node->base_type, p_class.node) + "::wgodot_get_native_interface(p_type);\n}\n\n";
	}
}
