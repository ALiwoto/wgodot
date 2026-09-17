// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"
#include "core/object/wgodot_native_interfaces.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

String WGodotCppEmitter::interface_cpp_type(const Parser::ClassNode *p_interface) {
	const auto *description = project.find_class(p_interface);
	class_native_headers.insert(description->cpp_name + ".h");
	return description->cpp_name + "_Interface";
}

void WGodotCppEmitter::emit_interface_inheritance(const WGodotCppProject::Class &p_class, String &r_bases, String &r_declaration, String &r_definitions) {
	String query;
	HashSet<StringName> forwarded;
	const auto &base = p_class.node->base_type;
	const String base_cpp = class_name(base, p_class.node);
	auto forward_method = [&](const String &p_interface, const StringName &p_name, const Parser::FunctionNode *p_required, const MethodInfo *p_native, bool p_inherited) {
		if (forwarded.has(p_name)) {
			return;
		}
		const auto *owner = member_owner(p_class.node, p_name);
		if (p_inherited && (!owner || owner->node != p_class.node)) {
			return;
		}
		const Parser::FunctionNode *method = owner && owner->node->get_member(p_name).type == Parser::ClassNode::Member::FUNCTION ? owner->node->get_member(p_name).function : nullptr;
		if (method && method->is_coroutine != (p_required && p_required->is_coroutine)) {
			unsupported(method, "interface implementation with a different coroutine result type");
			return;
		}
		MethodInfo native_method;
		if (!method && !ClassDB::get_method_info(native_base(base), p_name, &native_method)) {
			unsupported(p_class.node, "C++ interface implementation for " + String(p_name));
			return;
		}
		forwarded.insert(p_name);
		class_native_headers.insert("modules/wgodot/native/wgodot_native_interface.h");
		const String traits = "Interface_" + symbol(p_name);
		r_declaration += "\tusing " + traits + " = WGodotNative::InterfaceMethod<decltype(&" + p_interface + "::" + String(p_name) + ")>;\n";
		Vector<String> parameters;
		Vector<String> arguments;
		const int count = p_required ? int(p_required->parameters.size()) : p_native->arguments.size();
		for (int i = 0; i < count; i++) {
			const String argument = "p_argument_" + itos(i);
			parameters.push_back(traits + "::Argument<" + itos(i) + "> " + argument);
			arguments.push_back(method ? "WGodotNative::convert<" + type(method->parameters[i]->type_constraint, method->parameters[i]) + ">(" + argument + ")" : argument);
		}
		const bool constant = p_native && (p_native->flags & METHOD_FLAG_CONST);
		const String suffix = constant ? " const" : "";
		r_declaration += "\t" + traits + "::Result " + String(p_name) + "(" + String(", ").join(parameters) + ")" + suffix + " override;\n";
		String invoke;
		if (method) {
			invoke = String(constant ? "const_cast<" + p_class.cpp_name + " *>(this)->" : "this->") + "m_" + symbol(p_name) + "(" + String(", ").join(arguments) + ")";
		} else {
			const MethodBind *binding = ClassDB::get_method(native_base(base), p_name);
			const String *cpp_name = binding ? native_methods.getptr(String(binding->get_instance_class()) + "::" + String(p_name)) : nullptr;
			invoke = base_cpp + "::" + (cpp_name ? *cpp_name : String(p_name)) + "(" + String(", ").join(arguments) + ")";
		}
		const bool returns_value = p_required ? function_result(p_required) != "void" : p_native->return_val.type != Variant::NIL || (p_native->return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
		if (returns_value) {
			invoke = "WGodotNative::convert<" + traits + "::Result>(" + invoke + ")";
		}
		r_definitions += p_class.cpp_name + "::" + traits + "::Result " + p_class.cpp_name + "::" + String(p_name) + "(" + String(", ").join(parameters) + ")" + suffix + " { return " + invoke + "; }\n\n";
	};
	for (const StringName &name : p_class.node->wgodot_native_interfaces) {
		const auto *contract = WGodotNativeInterfaces::get_descriptor(name);
		used_native_interfaces.insert(name);
		for (const StringName &parent : contract->parents) {
			used_native_interfaces.insert(parent);
		}
		class_native_headers.insert(contract->cpp_header);
		const bool inherited = base.kind == Parser::DataType::CLASS ? WGodotGDScriptInterfaceHelpers::class_implements_native_interface(base.class_type, name) : WGodotNativeInterfaces::accepts(base.native_type, name);
		if (!inherited) {
			const WGodotNativeInterfaces::Descriptor *route = contract;
			for (const StringName &candidate_name : p_class.node->wgodot_native_interfaces) {
				const auto *candidate = WGodotNativeInterfaces::get_descriptor(candidate_name);
				if (candidate->parents.has(route->name)) {
					route = candidate;
				}
			}
			if (route == contract) {
				r_bases += ", public " + contract->cpp_type;
			}
			const String receiver = route == contract ? "this" : "static_cast<" + route->cpp_type + " *>(this)";
			query += "\tif (p_type == &" + contract->cpp_type + "::wgodot_interface_tag) { return static_cast<" + contract->cpp_type + " *>(" + receiver + "); }\n";
		}
		for (const auto &entry : contract->methods) {
			forward_method(contract->cpp_type, entry.key, nullptr, &entry.value, inherited);
		}
		bool script_signals = false;
		bool inherited_signals = false;
		for (const auto &entry : contract->signals) {
			script_signals = script_signals || member_owner(p_class.node, entry.key) != nullptr;
			inherited_signals = inherited_signals || (base.kind == Parser::DataType::CLASS && member_owner(base.class_type, entry.key));
		}
		if (script_signals) {
			const String signal_interface = native_interface_name(name) + "_Signals";
			class_native_headers.insert(native_interface_name(name) + ".h");
			if (!inherited_signals) {
				r_bases += ", public " + signal_interface;
				query += "\tif (p_type == &" + signal_interface + "::wgodot_interface_tag) { return static_cast<" + signal_interface + " *>(this); }\n";
			}
			for (const auto &entry : contract->signals) {
				const auto *owner = member_owner(p_class.node, entry.key);
				if (inherited_signals && (!owner || owner->node != p_class.node)) {
					continue;
				}
				const String getter = "signal_" + symbol(entry.key);
				if (forwarded.has(getter)) {
					continue;
				}
				forwarded.insert(getter);
				const String signal_type = native_interface_signal_type(entry.value);
				const String source = owner ? "s_" + symbol(entry.key) + ".signal()" : signal_type + "(Signal(this, SNAME(" + quoted(entry.key) + ")))";
				r_declaration += "\t" + signal_type + " " + getter + "() override;\n";
				r_definitions += signal_type + " " + p_class.cpp_name + "::" + getter + "() { return " + source + "; }\n";
			}
		}
	}
	for (const auto &contract : project.get_classes()) {
		if (!contract.node->wgodot_is_interface || !WGodotGDScriptInterfaceHelpers::class_implements_interface_type(p_class.node, contract.node)) {
			continue;
		}
		const String interface = interface_cpp_type(contract.node);
		const bool inherited = base.kind == Parser::DataType::CLASS && WGodotGDScriptInterfaceHelpers::class_implements_interface_type(base.class_type, contract.node);
		if (!inherited) {
			r_bases += ", public " + interface;
			query += "\tif (p_type == &" + interface + "::wgodot_interface_tag) { return static_cast<" + interface + " *>(this); }\n";
		}
		for (const auto &entry : contract.node->members) {
			const StringName name = entry.get_name();
			if (entry.type == Parser::ClassNode::Member::FUNCTION) {
				forward_method(interface, name, entry.function, nullptr, inherited);
				continue;
			}
			if (entry.type != Parser::ClassNode::Member::VARIABLE && entry.type != Parser::ClassNode::Member::SIGNAL) {
				continue;
			}
			const auto *owner = member_owner(p_class.node, name);
			if (inherited && (!owner || owner->node != p_class.node)) {
				continue;
			}
			const StringName forwarding_key = entry.type == Parser::ClassNode::Member::SIGNAL ? StringName("signal_" + symbol(name)) : name;
			if (forwarded.has(forwarding_key)) {
				continue;
			}
			forwarded.insert(forwarding_key);
			if (entry.type == Parser::ClassNode::Member::VARIABLE) {
				const String value_type = type(entry.variable->type_constraint, entry.variable);
				String read;
				String write;
				if (owner) {
					read = "read_" + symbol(name) + "()";
					write = "write_" + symbol(name) + "(p_value)";
				} else {
					const StringName getter = ClassDB::get_property_getter(native_base(base), name);
					const StringName setter = ClassDB::get_property_setter(native_base(base), name);
					const MethodBind *get = ClassDB::get_method(native_base(base), getter);
					const MethodBind *set = setter.is_empty() ? nullptr : ClassDB::get_method(native_base(base), setter);
					if (!get || (!entry.variable->wgodot_readonly && !set)) {
						unsupported(p_class.node, "native interface property " + String(name));
						continue;
					}
					read = native_invoke(get, lower_receiver(nullptr), {}, value_type, entry.variable).expression();
					if (set) {
						write = native_invoke(set, lower_receiver(nullptr), { Value("p_value", value_type) }, "void", entry.variable).expression();
					}
				}
				r_declaration += "\t" + value_type + " get_" + symbol(name) + "() override;\n";
				r_definitions += value_type + " " + p_class.cpp_name + "::get_" + symbol(name) + "() { return WGodotNative::convert<" + value_type + ">(" + read + "); }\n";
				if (!entry.variable->wgodot_readonly) {
					r_declaration += "\tvoid set_" + symbol(name) + "(" + value_type + " p_value) override;\n";
					r_definitions += "void " + p_class.cpp_name + "::set_" + symbol(name) + "(" + value_type + " p_value) { " + write + "; }\n";
				}
			} else {
				const String signal_type = signature_type(entry.signal, true);
				const String source = owner ? "s_" + symbol(name) + ".signal()" : signal_type + "(Signal(this, SNAME(" + quoted(name) + ")))";
				r_declaration += "\t" + signal_type + " signal_" + symbol(name) + "() override;\n";
				r_definitions += signal_type + " " + p_class.cpp_name + "::signal_" + symbol(name) + "() { return " + source + "; }\n";
			}
		}
		for (const StringName &name : contract.node->wgodot_native_interfaces) {
			const auto *native = WGodotNativeInterfaces::get_descriptor(name);
			for (const auto &entry : native->methods) {
				forward_method(interface, entry.key, nullptr, &entry.value, inherited);
			}
		}
	}
	if (!query.is_empty()) {
		r_declaration += "\tvoid *wgodot_get_native_interface(const void *p_type) override;\n";
		r_definitions += "void *" + p_class.cpp_name + "::wgodot_get_native_interface(const void *p_type) {\n" + query + "\treturn " + base_cpp + "::wgodot_get_native_interface(p_type);\n}\n\n";
	}
}
