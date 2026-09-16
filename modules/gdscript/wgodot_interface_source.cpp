// wgodot-changes::file
#include "wgodot_stdlib.h"

#include "core/object/class_db.h"
#include "core/variant/variant_parser.h"

namespace {
String source_type(const PropertyInfo &p_info, bool p_return = false) {
	if (p_info.type == Variant::NIL) {
		return p_return && !(p_info.usage & PROPERTY_USAGE_NIL_IS_VARIANT) ? "void" : "Variant";
	}
	if (p_info.type == Variant::OBJECT) {
		if (!p_info.class_name.is_empty()) {
			return p_info.class_name;
		}
		return (p_info.hint == PROPERTY_HINT_RESOURCE_TYPE || p_info.hint == PROPERTY_HINT_NODE_TYPE) && !p_info.hint_string.is_empty() ? p_info.hint_string : "Object";
	}
	if (p_info.type == Variant::ARRAY && p_info.hint == PROPERTY_HINT_ARRAY_TYPE) {
		return "Array[" + p_info.hint_string + "]";
	}
	return Variant::get_type_name(p_info.type);
}

String source_arguments(const MethodInfo &p_method) {
	Vector<String> arguments;
	const int first_default = p_method.arguments.size() - p_method.default_arguments.size();
	int index = 0;
	for (const PropertyInfo &argument : p_method.arguments) {
		String declaration = String(argument.name.is_empty() ? "p_argument_" + itos(index) : argument.name) + ": " + source_type(argument);
		if (index >= first_default) {
			String value;
			VariantWriter::write_to_string(p_method.default_arguments[index - first_default], value);
			declaration += " = " + value;
		}
		arguments.push_back(declaration);
		index++;
	}
	return String(", ").join(arguments);
}
} // namespace

String WGodotGDScriptStdLib::make_interface_source(const NativeInterface &p_interface) {
	String source = "interface_name " + String(p_interface.name) + "\nextends " + String(p_interface.native_base) + "\n";
	List<MethodInfo> methods(p_interface.methods);
	if (!p_interface.api_class.is_empty()) {
		// Reuse the native API's method signatures, defaults, properties and
		// signals. No second handwritten GDScript contract to keep synchronized.
		ClassDB::get_method_list(p_interface.api_class, &methods, true);
		List<StringName> enums;
		ClassDB::get_enum_list(p_interface.api_class, &enums, true);
		for (const StringName &name : enums) {
			List<StringName> constants;
			ClassDB::get_enum_constants(p_interface.api_class, name, &constants, true);
			Vector<String> values;
			for (const StringName &constant : constants) {
				values.push_back(String(constant) + " = " + itos(ClassDB::get_integer_constant(p_interface.api_class, constant)));
			}
			source += "enum " + String(name) + " { " + String(", ").join(values) + " }\n";
		}
		List<PropertyInfo> properties;
		ClassDB::get_property_list(p_interface.api_class, &properties, true);
		for (const PropertyInfo &property : properties) {
			if (property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
				continue;
			}
			const bool readonly = ClassDB::get_property_setter(p_interface.api_class, property.name).is_empty();
			source += String(readonly ? "@readonly " : "") + "var " + String(property.name) + ": " + source_type(property) + "\n";
		}
		List<MethodInfo> signals;
		ClassDB::get_signal_list(p_interface.api_class, &signals, true);
		for (const MethodInfo &signal : signals) {
			source += "signal " + String(signal.name) + "(" + source_arguments(signal) + ")\n";
		}
	}
	for (const MethodInfo &method : methods) {
		source += "func " + String(method.name) + "(" + source_arguments(method) + ") -> " + source_type(method.return_val, true) + "\n";
	}
	return source;
}
