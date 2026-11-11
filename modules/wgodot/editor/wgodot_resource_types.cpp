// wgodot-changes::file
#include "wgodot_resource_types.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/editor/export/export_analysis.h"
#include "modules/gdscript/wgodot_gd/editor/export/export_context.h"

using Parser = GDScriptParser;

WGodotResourceTypes::Qualification WGodotResourceTypes::Qualification::element(int p_index) const {
	return p_index < elements.size() ? elements[p_index] : Qualification();
}

String WGodotResourceTypes::Qualification::text() const {
	String result = immutable ? "i" : "m";
	for (const Qualification &child : elements) {
		result += "[" + child.text() + "]";
	}
	return result;
}

bool WGodotResourceTypes::Qualification::matches(const Qualification &p_other) const {
	if (immutable != p_other.immutable) {
		return false;
	}
	for (int i = 0; i < MAX(elements.size(), p_other.elements.size()); i++) {
		if (!element(i).matches(p_other.element(i))) {
			return false;
		}
	}
	return true;
}

WGodotResourceTypes::Qualification WGodotResourceTypes::script_qualification(const Parser::DataType &p_type) {
	Qualification result;
	result.immutable = p_type.wgodot_immutable;
	for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
		result.elements.push_back(script_qualification(p_type.get_container_element_type(i)));
	}
	return result;
}

WGodotResourceTypes::Qualification WGodotResourceTypes::native_qualification(const Type &p_type) {
	Qualification result;
	result.immutable = p_type.builtin == Variant::OBJECT && ClassDB::is_parent_class(p_type.name, SNAME("Resource"));
	for (const Type &element : p_type.elements) {
		result.elements.push_back(native_qualification(element));
	}
	return result;
}

WGodotResourceTypes::Type WGodotResourceTypes::script_type(const Parser::DataType &p_type) {
	Type result;
	if (!p_type.is_hard_type() || p_type.is_variant()) {
		return result;
	}
	result.resource_path = p_type.wgodot_resource_path;
	if (p_type.kind == Parser::DataType::ENUM) {
		result.builtin = Variant::INT;
	} else if (p_type.kind == Parser::DataType::CLASS) {
		result.builtin = Variant::OBJECT;
		result.name = p_type.class_type->get_global_name();
		if (result.name.is_empty()) {
			result.name = p_type.class_type->fqcn;
		}
	} else if (p_type.kind == Parser::DataType::NATIVE) {
		result.builtin = Variant::OBJECT;
		result.name = p_type.native_type;
	} else if (p_type.kind == Parser::DataType::BUILTIN) {
		result.builtin = p_type.builtin_type;
	}
	for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
		result.elements.push_back(script_type(p_type.get_container_element_type(i)));
	}
	return result;
}

WGodotResourceTypes::Type WGodotResourceTypes::native_type(const PropertyInfo &p_info) {
	return Type::from_property(p_info);
}

void WGodotResourceTypes::add_class(const Parser::ClassNode *p_class) {
	StringName name = p_class->get_global_name();
	if (name.is_empty()) {
		name = p_class->fqcn;
	}
	Class type;
	type.base = script_type(p_class->base_type).name;
	type.abstract = p_class->is_abstract || p_class->wgodot_is_interface;
	type.data_class = p_class->wgodot_data_class;
	Vector<String> signature_parts;
	signature_parts.push_back(String(type.base) + ":" + itos(type.abstract) + ":" + itos(type.data_class));
	for (const Parser::ClassNode::Member &member : p_class->members) {
		if (member.type == Parser::ClassNode::Member::VARIABLE) {
			const Parser::VariableNode *variable = member.variable;
			Field field;
			field.type = script_type(variable->type_constraint);
			field.qualification = script_qualification(variable->type_constraint);
			field.exposed = variable->exported && !variable->wgodot_private && !variable->wgodot_protected && !variable->is_static;
			type.fields.insert(variable->identifier->name, field);
			signature_parts.push_back(String(variable->identifier->name) + ":" + field.type.text() + ":" + itos(field.exposed) + ":" + field.qualification.text());
		} else if (member.type == Parser::ClassNode::Member::CLASS) {
			add_class(member.m_class);
		} else if (member.type == Parser::ClassNode::Member::FUNCTION || member.type == Parser::ClassNode::Member::SIGNAL) {
			const bool signal = member.type == Parser::ClassNode::Member::SIGNAL;
			Function function;
			const auto &parameters = signal ? member.signal->parameters : member.function->parameters;
			for (const auto *parameter : parameters) {
				function.parameters.push_back(script_type(parameter->type_constraint));
				function.qualifications.push_back(script_qualification(parameter->type_constraint));
				function.defaults += parameter->initializer != nullptr;
			}
			function.exposed = signal ? !member.signal->wgodot_private && !member.signal->wgodot_protected : !member.function->wgodot_private && !member.function->wgodot_protected && !member.function->is_static;
			function.variadic = !signal && member.function->is_vararg();
			(signal ? type.signals : type.methods).insert(member.get_name(), function);
			String signature = String(signal ? "signal:" : "method:") + String(member.get_name()) + ":" + itos(function.defaults) + ":" + itos(function.exposed) + ":" + itos(function.variadic);
			for (const Type &parameter : function.parameters) {
				signature += ":" + parameter.text();
			}
			for (const Qualification &qualification : function.qualifications) {
				signature += ":" + qualification.text();
			}
			signature_parts.push_back(signature);
		}
	}
	signature_parts.sort();
	type.signature = String("\n").join(signature_parts).sha256_text();
	classes.insert(name, type);
}

Error WGodotResourceTypes::build(const WGodotGDScriptExportTransform::ExportProject &p_project, String &r_error) {
	using namespace WGodotGDScriptExportTransform;
	ExportContext context;
	ExportAnalysis analysis(p_project, context, false);
	analysis.parse_scripts_parallel();
	ExportAnalysis::Scope scope(analysis);
	for (const String &path : p_project.get_script_paths()) {
		Error error = OK;
		Ref<GDScriptParserRef> parser = analysis.get_parser(path, GDScriptParserRef::INTERFACE_SOLVED, error);
		if (error != OK) {
			r_error = "Cannot resolve resource field declarations in " + path;
			return error;
		}
		add_class(parser->get_parser()->get_tree());
	}
	return OK;
}

bool WGodotResourceTypes::exists(const StringName &p_class) const {
	return classes.has(p_class) || ClassDB::class_exists(p_class);
}

bool WGodotResourceTypes::derives(const StringName &p_class, const StringName &p_base) const {
	StringName type = p_class;
	while (!type.is_empty()) {
		if (type == p_base) {
			return true;
		}
		const Class *script = classes.getptr(type);
		if (!script) {
			return ClassDB::is_parent_class(type, p_base);
		}
		type = script->base;
	}
	return false;
}

bool WGodotResourceTypes::instantiable(const StringName &p_class) const {
	const Class *script = classes.getptr(p_class);
	return script ? !script->abstract : ClassDB::can_instantiate(p_class);
}

bool WGodotResourceTypes::immutable_resource(const StringName &p_class) const {
	const Class *script = classes.getptr(p_class);
	return script ? script->data_class : ClassDB::is_parent_class(p_class, SNAME("Resource"));
}

bool WGodotResourceTypes::has_resource_exports(const StringName &p_class) const {
	const Class *type = classes.getptr(p_class);
	if (!type) {
		return true; // Native storage properties are declared by ClassDB.
	}
	while (type) {
		for (const auto &field : type->fields) {
			if (field.value.exposed) {
				return true;
			}
		}
		type = classes.getptr(type->base);
	}
	return false;
}

Error WGodotResourceTypes::check_exports(const StringName &p_class, String &r_error) const {
	for (const Class *type = classes.getptr(p_class); type; type = classes.getptr(type->base)) {
		for (const auto &field : type->fields) {
			if (field.value.exposed && !field.value.type.concrete()) {
				r_error = String(p_class) + "." + String(field.key) + " must have a concrete export type, including every container element/key/value.";
				return ERR_INVALID_DATA;
			}
		}
	}
	return OK;
}

String WGodotResourceTypes::signature(const StringName &p_class) const {
	const Class *script = classes.getptr(p_class);
	return script ? (script->signature + signature(script->base)).sha256_text() : ClassDB::class_exists(p_class) ? String("native")
																												 : String();
}

bool WGodotResourceTypes::field(const StringName &p_class, const StringName &p_name, Field &r_field, HashMap<String, String> &r_declarations) const {
	StringName name = p_class;
	while (!name.is_empty()) {
		if (!r_declarations.has(name)) {
			r_declarations.insert(name, signature(name));
		}
		const Class *script = classes.getptr(name);
		if (!script) {
			PropertyInfo info;
			if (!ClassDB::get_property_info(name, p_name, &info)) {
				return false;
			}
			r_field.type = native_type(info);
			r_field.qualification = native_qualification(r_field.type);
			r_field.exposed = (info.usage & PROPERTY_USAGE_STORAGE) != 0 && p_name != SNAME("script");
			return true;
		}
		if (const Field *field = script->fields.getptr(p_name)) {
			r_field = *field;
			record(r_field.type, r_declarations);
			return true;
		}
		name = script->base;
	}
	return false;
}

void WGodotResourceTypes::record(const Type &p_type, HashMap<String, String> &r_declarations) const {
	if (p_type.builtin == Variant::OBJECT && !r_declarations.has(p_type.name)) {
		r_declarations.insert(p_type.name, signature(p_type.name));
	}
	for (const Type &element : p_type.elements) {
		record(element, r_declarations);
	}
}

bool WGodotResourceTypes::function(const StringName &p_class, const StringName &p_name, bool p_signal, Function &r_function, HashMap<String, String> &r_declarations) const {
	StringName name = p_class;
	while (!name.is_empty()) {
		if (!r_declarations.has(name)) {
			r_declarations.insert(name, signature(name));
		}
		const Class *script = classes.getptr(name);
		if (!script) {
			MethodInfo info;
			if (!(p_signal ? ClassDB::get_signal(name, p_name, &info) : ClassDB::get_method_info(name, p_name, &info))) {
				return false;
			}
			r_function.defaults = info.default_arguments.size();
			r_function.variadic = (info.flags & METHOD_FLAG_VARARG) != 0;
			r_function.exposed = (info.flags & METHOD_FLAG_STATIC) == 0;
			for (const PropertyInfo &parameter : info.arguments) {
				r_function.parameters.push_back(native_type(parameter));
				r_function.qualifications.push_back(Qualification());
			}
			break;
		}
		if (const Function *function = (p_signal ? script->signals : script->methods).getptr(p_name)) {
			r_function = *function;
			break;
		}
		name = script->base;
	}
	if (name.is_empty()) {
		return false;
	}
	for (const Type &parameter : r_function.parameters) {
		record(parameter, r_declarations);
	}
	return true;
}

bool WGodotResourceTypes::accepts(const Type &p_expected, const Type &p_actual) const {
	if (!p_expected.concrete()) {
		return false;
	}
	if (p_actual.builtin == Variant::NIL) {
		return p_expected.builtin == Variant::OBJECT;
	}
	if (p_expected.builtin == Variant::FLOAT && p_actual.builtin == Variant::INT) {
		return true;
	}
	if (p_expected.builtin != p_actual.builtin) {
		return false;
	}
	if (p_expected.builtin == Variant::OBJECT) {
		return derives(p_actual.name, p_expected.name);
	}
	if (p_expected.elements.size() != p_actual.elements.size()) {
		return false;
	}
	for (int i = 0; i < p_expected.elements.size(); i++) {
		if (!accepts(p_expected.elements[i], p_actual.elements[i]) || !accepts(p_actual.elements[i], p_expected.elements[i])) {
			return false;
		}
	}
	return true;
}
