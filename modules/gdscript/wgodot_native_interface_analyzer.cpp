// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_gd/interface_helpers.h"

#include "core/object/wgodot_native_interfaces.h"

using Parser = GDScriptParser;
using DataType = Parser::DataType;
using Member = Parser::ClassNode::Member;

bool GDScriptAnalyzer::wgodot_reduce_native_interface_member(const DataType &p_base, Parser::IdentifierNode *p_identifier) {
	const StringName name = p_identifier->name;
	const auto *contract = WGodotGDScriptInterfaceHelpers::native_interface_for_member(p_base, name);
	if (!contract) {
		return false;
	}
	DataType &type = p_identifier->type_constraint;
	const MethodInfo *method = contract->methods.getptr(name);
	const MethodInfo *signal = contract->signals.getptr(name);
	if (method || signal) {
		type = DataType();
		type.kind = DataType::BUILTIN;
		type.type_source = DataType::ANNOTATED_EXPLICIT;
		type.builtin_type = method ? Variant::CALLABLE : Variant::SIGNAL;
		type.is_constant = true;
		type.method_info = method ? *method : *signal;
		p_identifier->source = Parser::IdentifierNode::INHERITED_VARIABLE;
		return true;
	}
	if (const auto *property = contract->properties.getptr(name)) {
		type = type_from_property(property->info, false, p_identifier);
		type.is_read_only = property->setter.is_empty();
		p_identifier->source = Parser::IdentifierNode::INHERITED_VARIABLE;
		return true;
	}
	if (const auto *values = contract->enums.getptr(name)) {
		type = DataType();
		type.kind = DataType::ENUM;
		type.type_source = DataType::ANNOTATED_EXPLICIT;
		type.builtin_type = Variant::NIL;
		type.enum_type = name;
		type.native_type = String(contract->name) + "." + String(name);
		type.is_meta_type = type.is_pseudo_type = type.is_constant = true;
		for (const auto &entry : *values) {
			type.enum_values[entry.key] = entry.value;
		}
		p_identifier->source = Parser::IdentifierNode::MEMBER_CONSTANT;
		return true;
	}
	if (const int64_t *value = contract->constants.getptr(name)) {
		type = type_from_variant(*value, p_identifier);
		for (const auto &entry : contract->enums) {
			if (entry.value.has(name)) {
				type.kind = DataType::ENUM;
				type.enum_type = entry.key;
				type.native_type = String(contract->name) + "." + String(entry.key);
				break;
			}
		}
		p_identifier->is_constant = true;
		p_identifier->reduced_value = *value;
		p_identifier->source = Parser::IdentifierNode::MEMBER_CONSTANT;
		return true;
	}
	return false;
}

void GDScriptAnalyzer::wgodot_validate_native_interfaces(Parser::ClassNode *p_class) {
	for (const StringName &name : p_class->wgodot_native_interfaces) {
		const auto *contract = WGodotNativeInterfaces::get_descriptor(name);
		const bool native_implementation = WGodotNativeInterfaces::accepts(p_class->self_type.native_type, name);
		auto find_member = [&](const StringName &p_name, Member &r_member) {
			Parser::ClassNode *owner = wgodot_find_member_owner(p_class, p_name);
			if (!owner) {
				if (!p_class->wgodot_is_interface && !native_implementation) {
					push_error(vformat("Class '%s' must implement '%s.%s'.", wgodot_get_class_display_name(p_class), name, p_name), p_class);
				}
				return false;
			}
			resolve_class_member(owner, p_name, p_class);
			r_member = owner->get_member(p_name);
			return true;
		};
		auto mismatch = [&](const StringName &p_member) {
			push_error(vformat("Class '%s' has an incompatible implementation of native interface member '%s.%s'.", wgodot_get_class_display_name(p_class), name, p_member), p_class);
		};
		for (const auto &entry : contract->methods) {
			Member actual;
			if (!find_member(entry.key, actual)) {
				continue;
			}
			if (actual.type != Member::FUNCTION) {
				mismatch(entry.key);
				continue;
			}
			Parser::FunctionNode *function = actual.function;
			function->wgodot_interface_implementation = true;
			const MethodInfo &required = entry.value;
			bool valid = !function->is_static && !function->wgodot_private && !function->wgodot_protected && !function->is_coroutine;
			const int required_min = required.arguments.size() - required.default_arguments.size();
			const int actual_min = function->parameters.size() - function->default_arg_values.size();
			valid = valid && actual_min <= required_min && int(function->parameters.size()) >= required.arguments.size();
			int index = 0;
			for (const PropertyInfo &argument : required.arguments) {
				if (index >= int(function->parameters.size())) {
					break;
				}
				valid = valid && wgodot_interface_type_accepts(function->parameters[index++]->type_constraint, type_from_property(argument, true, function));
			}
			valid = valid && wgodot_interface_type_accepts(type_from_property(required.return_val, false, function), function->return_type_constraint);
			if (!valid) {
				mismatch(entry.key);
			}
		}
		for (const auto &entry : contract->properties) {
			Member actual;
			if (!find_member(entry.key, actual)) {
				continue;
			}
			if (actual.type != Member::VARIABLE) {
				mismatch(entry.key);
				continue;
			}
			Parser::VariableNode *variable = actual.variable;
			variable->wgodot_interface_implementation = true;
			const DataType required = type_from_property(entry.value.info, false, variable);
			bool valid = !variable->is_static && !variable->wgodot_private && !variable->wgodot_protected && wgodot_interface_type_accepts(required, variable->type_constraint);
			if (!entry.value.setter.is_empty()) {
				valid = valid && !variable->wgodot_readonly && wgodot_interface_type_accepts(variable->type_constraint, required);
			}
			if (!valid) {
				mismatch(entry.key);
			}
		}
		for (const auto &entry : contract->signals) {
			Member actual;
			if (!find_member(entry.key, actual)) {
				continue;
			}
			if (actual.type != Member::SIGNAL) {
				mismatch(entry.key);
				continue;
			}
			Parser::SignalNode *signal = actual.signal;
			signal->wgodot_interface_implementation = true;
			bool valid = !signal->wgodot_private && !signal->wgodot_protected && int(signal->parameters.size()) == entry.value.arguments.size();
			int index = 0;
			for (const PropertyInfo &argument : entry.value.arguments) {
				if (index >= int(signal->parameters.size())) {
					break;
				}
				const DataType required = type_from_property(argument, true, signal);
				const DataType &provided = signal->parameters[index++]->type_constraint;
				valid = valid && wgodot_interface_type_accepts(required, provided) && wgodot_interface_type_accepts(provided, required);
			}
			if (!valid) {
				mismatch(entry.key);
			}
		}
	}
}
