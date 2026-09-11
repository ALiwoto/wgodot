// wgodot-changes::file

#include "gdscript_analyzer.h"
#include "wgodot_gd/interface_helpers.h"
#include "wgodot_stdlib.h"

#include "core/object/class_db.h"

namespace {

using ClassNode = GDScriptParser::ClassNode;
using DataType = GDScriptParser::DataType;

ClassNode *find_interface(ClassNode *p_class, const String &p_name) {
	if (p_class->fqcn == p_name) {
		return p_class;
	}
	for (const ClassNode::Member &member : p_class->members) {
		if (member.type == ClassNode::Member::CLASS) {
			ClassNode *found = find_interface(member.m_class, p_name);
			if (found != nullptr) {
				return found;
			}
		}
	}
	return nullptr;
}

} // namespace

void GDScriptAnalyzer::wgodot_resolve_implemented_interfaces(ClassNode *p_class) {
	if (p_class->wgodot_interfaces_resolved) {
		return;
	}
	p_class->wgodot_resolving_interfaces = true;
	Vector<ClassNode *> direct;
	if (p_class->base_type.kind == DataType::CLASS && p_class->base_type.class_type->wgodot_is_interface) {
		if (!p_class->wgodot_is_interface) {
			push_error("Use 'implements' to implement an interface; 'extends' selects the concrete parent class.", p_class);
		} else {
			direct.push_back(p_class->base_type.class_type);
		}
	}
	for (const ClassNode::WGodotInterfaceReference &reference : p_class->wgodot_implements) {
		ClassNode *contract = wgodot_resolve_interface_reference(p_class, reference);
		if (contract != nullptr) {
			direct.push_back(contract);
		}
	}
	HashSet<String> seen;
	StringName required_base = p_class->self_type.native_type;
	if (required_base.is_empty()) {
		required_base = SNAME("Object");
	}
	for (ClassNode *contract : direct) {
		if (!contract->wgodot_is_interface) {
			push_error(vformat("'%s' is not an interface.", wgodot_get_class_display_name(contract)), p_class);
			continue;
		}
		if (contract->wgodot_resolving_interfaces) {
			push_error(vformat("Cyclic interface inheritance involving '%s'.", wgodot_get_class_display_name(contract)), p_class);
			continue;
		}
		resolve_class_interface(contract, p_class);
		const StringName contract_base = contract->self_type.native_type;
		if (!ClassDB::is_parent_class(required_base, contract_base)) {
			if (p_class->wgodot_is_interface && ClassDB::is_parent_class(contract_base, required_base)) {
				required_base = contract_base;
			} else {
				push_error(vformat("Interface '%s' requires a '%s' base; '%s' derives from '%s'.", wgodot_get_class_display_name(contract), contract_base, wgodot_get_class_display_name(p_class), required_base), p_class);
			}
		}
		Vector<ClassNode *> inherited = contract->wgodot_resolved_interfaces;
		inherited.push_back(contract);
		for (ClassNode *ancestor : inherited) {
			const String id = WGodotGDScriptInterfaceHelpers::get_interface_id(ancestor);
			if (!seen.has(id)) {
				seen.insert(id);
				p_class->wgodot_resolved_interfaces.push_back(ancestor);
			}
		}
	}
	if (p_class->wgodot_is_interface) {
		p_class->wgodot_own_member_count = p_class->members.size();
		if (p_class->base_type.kind == DataType::CLASS && !p_class->base_type.class_type->wgodot_is_interface) {
			push_error("An interface can extend another interface or require a native base, but cannot inherit a script implementation.", p_class);
		} else {
			p_class->base_type = DataType();
			p_class->base_type.kind = DataType::NATIVE;
			p_class->base_type.type_source = DataType::ANNOTATED_EXPLICIT;
			p_class->base_type.builtin_type = Variant::OBJECT;
			p_class->base_type.native_type = required_base;
			p_class->self_type.native_type = required_base;
		}
		// Imported members are already resolved in their declaring parser. Keeping
		// their declarations makes normal member lookup, completion and compilation
		// see the full contract without duplicating the native Control API.
		for (ClassNode *contract : p_class->wgodot_resolved_interfaces) {
			for (const ClassNode::Member &member : contract->members) {
				if (member.type != ClassNode::Member::GROUP && !p_class->has_member(member.get_name())) {
					p_class->members_indices[member.get_name()] = p_class->members.size();
					p_class->members.push_back(member);
				}
			}
		}
	}
	p_class->wgodot_resolving_interfaces = false;
	p_class->wgodot_interfaces_resolved = true;
}

bool GDScriptAnalyzer::wgodot_interface_type_accepts(const DataType &p_target, const DataType &p_source) {
	if (p_source.has_no_type()) {
		return p_target.has_no_type();
	}
	if (p_source.kind == DataType::BUILTIN && p_source.builtin_type == Variant::NIL && p_target.is_hard_type()) {
		return p_target.kind == DataType::BUILTIN && p_target.builtin_type == Variant::NIL;
	}
	if (p_target.has_no_type()) {
		return true;
	}
	// A native method can return its own class while that class's interface
	// registration is being validated, before membership is published to ClassDB.
	if (wgodot_validating_native_interface != nullptr && p_target.kind == DataType::CLASS && p_source.kind == DataType::NATIVE &&
			ClassDB::is_parent_class(p_source.native_type, wgodot_validating_native_class) &&
			WGodotGDScriptInterfaceHelpers::class_implements_interface_type(wgodot_validating_native_interface, p_target.class_type)) {
		return true;
	}
	return is_type_compatible(p_target, p_source);
}

bool GDScriptAnalyzer::wgodot_interface_native_method_matches(const GDScriptParser::FunctionNode *p_method, const MethodInfo &p_native, String &r_error) {
	if (p_native.flags & METHOD_FLAG_STATIC) {
		r_error = "the native method is static";
		return false;
	}
	const int expected_min = p_method->parameters.size() - p_method->default_arg_values.size();
	const int actual_min = p_native.arguments.size() - p_native.default_arguments.size();
	const bool native_vararg = p_native.flags & METHOD_FLAG_VARARG;
	if (actual_min > expected_min || (!native_vararg && (p_method->is_vararg() || p_native.arguments.size() < int(p_method->parameters.size())))) {
		r_error = "the native method does not accept the contract's argument range";
		return false;
	}
	int index = 0;
	for (const PropertyInfo &argument : p_native.arguments) {
		if (index >= int(p_method->parameters.size()) && !p_method->is_vararg()) {
			break;
		}
		const DataType actual = type_from_property(argument, true, p_method);
		const DataType expected = index < int(p_method->parameters.size()) ? p_method->parameters[index]->type_constraint : DataType::get_variant_type();
		if (!wgodot_interface_type_accepts(actual, expected)) {
			r_error = vformat("parameter %d accepts '%s', but the contract requires '%s'", index + 1, actual.to_string(), expected.to_string());
			return false;
		}
		index++;
	}
	const DataType actual_return = type_from_property(p_native.return_val, false, p_method);
	if (!wgodot_interface_type_accepts(p_method->return_type_constraint, actual_return)) {
		r_error = vformat("returns '%s', but the contract requires '%s'", actual_return.to_string(), p_method->return_type_constraint.to_string());
		return false;
	}
	return true;
}

bool GDScriptAnalyzer::wgodot_reduce_interface_identifier(GDScriptParser::IdentifierNode *p_identifier, const StringName &p_name) const {
	if (!wgodot_type_from_interface_property(PropertyInfo(p_name), p_identifier, p_identifier->type_constraint)) {
		return false;
	}
	p_identifier->type_constraint.is_meta_type = true;
	p_identifier->type_constraint.is_constant = true;
	return true;
}

bool GDScriptAnalyzer::wgodot_type_from_interface_property(const PropertyInfo &p_property, const GDScriptParser::Node *p_source, GDScriptParser::DataType &r_type) const {
	if (!WGodotGDScriptStdLib::has_global_interface(p_property.class_name)) {
		return false;
	}
	const String path = WGodotGDScriptStdLib::get_global_interface_path(p_property.class_name);
	if (GDScript::is_canonically_equal_paths(path, parser->script_path)) {
		r_type = type_from_metatype(parser->get_tree()->self_type);
		return true;
	}
	Ref<GDScriptParserRef> contract = parser->get_depended_parser_for(path);
	if (contract.is_null() || contract->raise_status(GDScriptParserRef::INTERFACE_SOLVED) != OK) {
		push_error(vformat("Could not resolve interface '%s' in a reflected property or signature.", p_property.class_name), p_source);
		r_type.kind = DataType::VARIANT;
		return true;
	}
	r_type = type_from_metatype(contract->get_parser()->get_tree()->self_type);
	return true;
}

bool GDScriptAnalyzer::wgodot_interface_native_member_matches(const ClassNode::Member &p_member, const StringName &p_native_class, String &r_error) {
	const StringName name = p_member.get_name();
	switch (p_member.type) {
		case ClassNode::Member::FUNCTION: {
			MethodInfo method;
			if (!ClassDB::get_method_info(p_native_class, name, &method)) {
				r_error = "missing native method";
				return false;
			}
			return wgodot_interface_native_method_matches(p_member.function, method, r_error);
		}
		case ClassNode::Member::VARIABLE: {
			PropertyInfo property;
			if (!ClassDB::get_property_info(p_native_class, name, &property)) {
				r_error = "missing native property";
				return false;
			}
			// Node picker hints do not populate PropertyInfo::class_name. The
			// getter's binding still carries the actual C++ return type.
			if (property.type == Variant::OBJECT && property.class_name.is_empty()) {
				MethodInfo getter;
				if (ClassDB::get_method_info(p_native_class, ClassDB::get_property_getter(p_native_class, name), &getter)) {
					property = getter.return_val;
				}
			}
			const DataType actual = type_from_property(property, false, p_member.variable);
			const DataType &expected = p_member.variable->type_constraint;
			if (!wgodot_interface_type_accepts(expected, actual) || (!p_member.variable->wgodot_readonly && !wgodot_interface_type_accepts(actual, expected))) {
				r_error = "incompatible native property type";
				return false;
			}
			if (ClassDB::get_property_getter(p_native_class, name).is_empty() || (!p_member.variable->wgodot_readonly && ClassDB::get_property_setter(p_native_class, name).is_empty())) {
				r_error = "native property does not provide the required read/write access";
				return false;
			}
			return true;
		}
		case ClassNode::Member::SIGNAL: {
			MethodInfo signal;
			if (!ClassDB::get_signal(p_native_class, name, &signal) || signal.arguments.size() != int(p_member.signal->parameters.size())) {
				r_error = "missing native signal or incompatible argument count";
				return false;
			}
			int index = 0;
			for (const PropertyInfo &argument : signal.arguments) {
				const DataType actual = type_from_property(argument, true, p_member.signal);
				const DataType &expected = p_member.signal->parameters[index++]->type_constraint;
				if (!wgodot_interface_type_accepts(expected, actual) || !wgodot_interface_type_accepts(actual, expected)) {
					r_error = "incompatible native signal argument type";
					return false;
				}
			}
			return true;
		}
		default:
			return true; // Constants and enums belong to the contract itself.
	}
}

Error GDScriptAnalyzer::wgodot_validate_native_interface(const String &p_qualified_name, const StringName &p_native_class, String &r_error) {
	ClassNode *contract = find_interface(parser->get_tree(), p_qualified_name);
	if (contract == nullptr || !contract->wgodot_is_interface) {
		r_error = "interface declaration was not found";
		return ERR_INVALID_DATA;
	}
	if (!ClassDB::is_parent_class(p_native_class, contract->self_type.native_type)) {
		r_error = vformat("requires native base '%s'", contract->self_type.native_type);
		return ERR_INVALID_DATA;
	}
	wgodot_validating_native_class = p_native_class;
	wgodot_validating_native_interface = contract;
	Error result = OK;
	for (const ClassNode::Member &member : contract->members) {
		if (!wgodot_interface_native_member_matches(member, p_native_class, r_error)) {
			r_error = vformat("%s: %s", member.get_name(), r_error);
			result = ERR_INVALID_DATA;
			break;
		}
	}
	wgodot_validating_native_class = StringName();
	wgodot_validating_native_interface = nullptr;
	return result;
}
