// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_interface(const WGodotCppProject::Class &p_class) {
	const StringName native = native_base(p_class.node->self_type);
	const String *native_header = native_headers.getptr(native);
	if (!native_header) {
		unsupported(p_class.node, "native interface base " + String(native));
		return;
	}
	const String &name = p_class.cpp_name;
	const String qualified = "WGodotGame::" + name;
	const String base = "WGodotNative::InterfaceValue<" + name + ", " + native_cpp_names[native] + ">";
	String header = "// wgodot-changes::file\n// Generated native interface value.\n#pragma once\n#include \"modules/wgodot/native/wgodot_native_interface.h\"\n#include \"" + *native_header + "\"\n\nnamespace WGodotGame {\n";
	header += "class " + name + " : public " + base + " {\npublic:\n\tusing " + base + "::InterfaceValue;\n\tstatic const StringName &get_class_static() { static const StringName name = \"" + name + "\"; return name; }\n};\n} // namespace WGodotGame\n\n";
	header += "template <> struct GetTypeInfo<" + qualified + "> : WGodotNative::InterfaceTypeInfo<" + qualified + "> {};\n";
	header += "template <> struct PtrToArg<" + qualified + "> : WGodotNative::InterfacePtrToArg<" + qualified + "> {};\n";
	header += "template <> struct VariantInternalAccessor<" + qualified + "> : WGodotNative::InterfaceVariantAccessor<" + qualified + "> {};\n";
	header += "template <> struct VariantObjectClassChecker<" + qualified + "> { static bool check(const Variant &p_value) { return " + qualified + "::accepts(p_value); } };\n\n";
	// TypedArray/TypedDictionary need the contract's identity, although the C++
	// value itself is not an Object subclass. Keep these extensions generated.
	header += "namespace GodotTypeInfo::Internal {\ntemplate <> inline const StringName &get_object_class_name_or_empty<" + qualified + ">() { return " + qualified + "::get_class_static(); }\n";
	header += "template <> inline const String get_variant_type_identifier<" + qualified + ">() { return " + qualified + "::get_class_static(); }\n}\n";
	files.insert(name + ".h", header);
}

String WGodotCppEmitter::register_interfaces() {
	String contracts;
	String implementations;
	LocalVector<StringName> native_classes;
	ClassDB::get_class_list(native_classes);
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
		contracts += "\tWGodotNativeInterfaces::add_contract(SNAME(" + quoted(entry.cpp_name) + "), SNAME(" + quoted(native_base(entry.node->self_type)) + "), {" + String(", ").join(parents) + "});\n";
		const String source_id = WGodotGDScriptInterfaceHelpers::get_interface_id(entry.node);
		for (const StringName &native : native_classes) {
			if (ClassDB::wgodot_class_implements_interface(native, source_id)) {
				implementations += "\tWGodotNativeInterfaces::add_implementation(SNAME(" + quoted(entry.cpp_name) + "), SNAME(" + quoted(native) + "));\n";
			}
		}
		for (const auto &candidate : project.get_classes()) {
			if (!candidate.node->wgodot_is_interface && !candidate.node->wgodot_static_class && WGodotGDScriptInterfaceHelpers::class_implements_interface_type(candidate.node, entry.node)) {
				implementations += "\tWGodotNativeInterfaces::add_implementation(SNAME(" + quoted(entry.cpp_name) + "), SNAME(" + quoted(candidate.cpp_name) + "));\n";
			}
		}
	}
	return contracts + implementations;
}
