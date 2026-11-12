// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_record(const WGodotCppProject::Class &p_class) {
	const String &name = p_class.cpp_name;
	String declaration = "class " + name + " : public WGodotNative::RecordValue<" + name + "> {\npublic:\n";
	declaration += "\t" + name + "(const Variant &p_value);\n\t" + name + "(const " + name + " &);\n\t" + name + " &operator=(const " + name + " &);\n\t~" + name + "();\n";
	declaration += "\tstatic StringName record_name() { return SNAME(" + quoted(p_class.node->fqcn) + "); }\n";
	Vector<String> parameters;
	Vector<String> default_parameters;
	Vector<String> initialization;
	String visit;
	Vector<String> comparisons;
	for (const auto &member : p_class.node->members) {
		if (member.type != Parser::ClassNode::Member::VARIABLE) {
			continue;
		}
		const auto *variable = member.variable;
		const auto &datatype = variable->type_constraint;
		const String field_type = type(datatype, variable);
		const String field = "v_" + symbol(variable->identifier->name);
		const bool container = is_warray(datatype) || is_wdictionary(datatype) || is_packed(datatype);
		declaration += "\t" + field_type + " " + field + ";\n";
		const String argument = "p_" + symbol(variable->identifier->name);
		parameters.push_back(field_type + " const *" + argument);
		default_parameters.push_back(parameters[parameters.size() - 1] + " = nullptr");
		if (datatype.kind == Parser::DataType::CLASS && datatype.class_type->wgodot_record) {
			class_native_headers.insert(field_type + ".h");
		}
		String initial = "decltype(" + field + ")()";
		if (variable->initializer) {
			initial = converted(variable->initializer, datatype, variable);
		} else if (container) {
			initial = "WGodotNative::value_container(" + initial + ")";
		}
		// Supplied fields bypass their default expressions. Constructing all
		// defaults before applying overrides would recurse forever for finite
		// trees whose children explicitly override a populated default array.
		initialization.push_back(field + "(" + argument + " ? *" + argument + " : " + initial + ")");
		visit += "\t\tp_visit(SNAME(" + quoted(variable->identifier->name) + "), p_record." + field + ", " + String(datatype.wgodot_value ? "true" : "false") + ");\n";
		comparisons.push_back("WGodotNative::record_field_equal(" + field + ", p_other." + field + ")");
	}
	declaration += "\t" + name + "(" + String(", ").join(default_parameters) + ");\n";
	declaration += "\ttemplate <class Self, class Visitor>\n\tstatic void visit_fields(Self &p_record, Visitor &&p_visit) {\n" + visit + "\t}\n";
	declaration += "\tbool operator==(const " + name + " &p_other) const;\n\tbool operator!=(const " + name + " &p_other) const { return !(*this == p_other); }\n";
	String definitions = name + "::" + name + "(" + String(", ").join(parameters) + ")";
	if (!initialization.is_empty()) {
		definitions += " :\n\t\t" + String(",\n\t\t").join(initialization);
	}
	definitions += " {}\n\n";
	definitions += name + "::" + name + "(const Variant &p_value) : " + name + "() {\n\tERR_FAIL_COND_MSG(!read_record(p_value), \"Invalid native record value.\");\n}\n\n";
	definitions += name + "::" + name + "(const " + name + " &) = default;\n";
	definitions += name + " &" + name + "::operator=(const " + name + " &) = default;\n";
	definitions += name + "::~" + name + "() = default;\n\n";
	definitions += "bool " + name + "::operator==(const " + name + " &p_other) const {\n\treturn " + (comparisons.is_empty() ? String("true") : String(" &&\n\t\t\t").join(comparisons)) + ";\n}\n\n";
	emit_container_constants(p_class, declaration, definitions);
	declaration += "};\n";
	const auto location = project.find_parser(p_class.script_path)->wgodot_source_location(p_class.node->start_line);
	const String origin = source_header(location.path, p_class.node->fqcn);
	String header = origin + "#pragma once\n#include \"game_types.h\"\n#include \"modules/wgodot/native/wgodot_native_record.h\"\n";
	Vector<String> includes;
	for (const String &include : class_native_headers) {
		includes.push_back(include);
	}
	includes.sort();
	for (const String &include : includes) {
		header += "#include " + quoted(include) + "\n";
	}
	header += "\nnamespace WGodotGame {\n";
	Vector<String> dependencies;
	for (const String &dependency : class_dependencies) {
		if (dependency != name) {
			dependencies.push_back(dependency);
		}
	}
	dependencies.sort();
	for (const String &dependency : dependencies) {
		header += "class " + dependency + ";\n";
	}
	header += declaration + "} // namespace WGodotGame\n";
	String source = origin + "#include " + quoted(name + ".h") + "\n";
	includes.clear();
	for (const String &include : class_call_headers) {
		includes.push_back(include);
	}
	includes.sort();
	for (const String &include : includes) {
		source += "#include " + quoted(include) + "\n";
	}
	for (const String &dependency : dependencies) {
		source += "#include " + quoted(dependency + ".h") + "\n";
	}
	source += "\nnamespace WGodotGame {\n" + definitions + "} // namespace WGodotGame\n";
	store_file(name + ".h", header);
	store_file(name + ".cpp", source);
}
