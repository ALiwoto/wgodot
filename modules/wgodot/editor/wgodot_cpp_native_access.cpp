// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/method_bind.h"

using namespace WGodotCppNames;

String WGodotCppEmitter::native_access(const MethodBind *p_method, const GDScriptParser::Node *p_origin, bool p_qualified) {
	const StringName owner = p_method->get_instance_class();
	const String key = String(owner) + "::" + String(p_method->get_name());
	const String *method = native_methods.getptr(key);
	if (!method) {
		unsupported(p_origin, "direct native access to " + key + "; no unambiguous C++ binding mapping is available");
		return String();
	}
	GDScriptParser::DataType owner_type;
	owner_type.kind = GDScriptParser::DataType::NATIVE;
	owner_type.native_type = owner;
	const String cpp_owner = "::" + class_name(owner_type, p_origin);
	const String name = String(p_qualified ? "base_" : "call_") + symbol(p_method->get_name());
	String definition = "\ttemplate <";
	if (p_qualified && !p_method->is_static()) {
		definition += "class Base, ";
	}
	definition += "class... Args>\n\tstatic decltype(auto) " + name + "(";
	String receiver;
	if (!p_method->is_static()) {
		definition += String(p_method->is_const() ? "const " : "") + (p_qualified ? "Base" : cpp_owner) + " *p_self, ";
		// Interface forwarding must bypass the generated overriding method.
		receiver = p_qualified ? "p_self->Base::" : "p_self->";
	} else {
		receiver = cpp_owner + "::";
	}
	definition += "Args &&...p_args) {\n\t\treturn " + receiver + *method + "(std::forward<Args>(p_args)...);\n\t}\n";
	native_access_methods[owner].insert(name, definition);
	class_call_headers.insert("native_access_" + symbol(owner) + ".h");
	return "WGodotNative::EngineAccess<" + cpp_owner + ">::" + name;
}

void WGodotCppEmitter::emit_native_access() {
	for (const auto &owner : native_access_methods) {
		String header = "// wgodot-changes::file\n#pragma once\n#include \"core/object/wgodot_native_access.h\"\n";
		header += "#include " + quoted(native_headers[owner.key]) + "\n#include <utility>\n\nnamespace WGodotNative {\n";
		header += "template <>\nstruct EngineAccess<::" + native_cpp_names[owner.key] + "> {\n";
		Vector<String> methods;
		for (const auto &method : owner.value) {
			methods.push_back(method.key);
		}
		methods.sort();
		for (const String &method : methods) {
			header += owner.value[method];
		}
		header += "};\n} // namespace WGodotNative\n";
		files.insert("native_access_" + symbol(owner.key) + ".h", header);
	}
}
