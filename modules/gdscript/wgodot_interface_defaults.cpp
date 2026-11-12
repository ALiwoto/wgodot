// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_gd/interface_helpers.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;
using DataType = Parser::DataType;
using Member = Parser::ClassNode::Member;

void GDScriptAnalyzer::wgodot_resolve_interface_defaults(Parser::ClassNode *p_class) {
	HashMap<StringName, Vector<DataType>> declarations;
	Vector<Parser::ClassNode *> contracts = p_class->wgodot_resolved_interfaces;
	if (p_class->wgodot_is_interface) {
		contracts.push_back(p_class);
	}
	for (auto *contract : contracts) {
		for (int i = 0; i < contract->wgodot_own_member_count; i++) {
			const Member &member = contract->members[i];
			if (member.type == Member::FUNCTION) {
				declarations[member.get_name()].push_back(contract->self_type);
			}
		}
	}
	for (const StringName &name : p_class->wgodot_native_interfaces) {
		const auto *contract = WGodotNativeInterfaces::get_descriptor(name);
		DataType type;
		type.kind = DataType::NATIVE;
		type.type_source = DataType::ANNOTATED_EXPLICIT;
		type.builtin_type = Variant::OBJECT;
		type.native_type = name;
		for (const auto &method : contract->methods) {
			declarations[method.key].push_back(type);
		}
	}
	const auto derives = [](const DataType &p_child, const DataType &p_parent) {
		if (p_child.kind == DataType::CLASS) {
			return p_parent.kind == DataType::CLASS ? p_child.class_type->wgodot_resolved_interfaces.has(p_parent.class_type) : p_child.class_type->wgodot_native_interfaces.has(p_parent.native_type);
		}
		return p_parent.kind == DataType::NATIVE && WGodotNativeInterfaces::get_descriptor(p_child.native_type)->parents.has(p_parent.native_type);
	};
	p_class->wgodot_default_methods.clear();
	for (const auto &entry : declarations) {
		if (!p_class->wgodot_is_interface && (wgodot_find_member_owner(p_class, entry.key) || ClassDB::has_method(p_class->self_type.native_type, entry.key))) {
			continue;
		}
		Vector<DataType> defaults;
		for (int i = 0; i < entry.value.size(); i++) {
			const DataType &candidate = entry.value[i];
			bool hidden = false;
			for (int j = 0; j < entry.value.size(); j++) {
				if (i != j && derives(entry.value[j], candidate)) {
					hidden = true;
					break;
				}
			}
			if (hidden) {
				continue;
			}
			const bool has_body = candidate.kind == DataType::CLASS ? !candidate.class_type->get_member(entry.key).function->is_abstract : WGodotNativeInterfaces::get_descriptor(candidate.native_type)->default_methods.has(entry.key);
			if (has_body) {
				defaults.push_back(candidate);
			}
		}
		if (defaults.size() > 1) {
			push_error(vformat("Multiple interfaces provide a default body for '%s'. Declare the method explicitly and select an implementation with super_as(InterfaceName).%s().", entry.key, entry.key), p_class);
		} else if (defaults.size() == 1) {
			p_class->wgodot_default_methods.insert(entry.key, defaults[0]);
		}
	}
}

bool GDScriptAnalyzer::wgodot_resolve_super_as(Parser::CallNode *p_call, DataType &r_type) {
	r_type = type_from_metatype(resolve_datatype(p_call->wgodot_super_type));
	const auto *current = parser->current_class;
	bool allowed = false;
	bool interface = false;
	if (r_type.kind == DataType::CLASS && r_type.class_type->wgodot_is_interface) {
		interface = true;
		allowed = current != r_type.class_type && WGodotGDScriptInterfaceHelpers::class_implements_interface_type(current, r_type.class_type);
	} else if (r_type.kind == DataType::NATIVE && WGodotNativeInterfaces::get_descriptor(r_type.native_type)) {
		interface = true;
		allowed = WGodotGDScriptInterfaceHelpers::class_implements_native_interface(current, r_type.native_type);
	} else {
		for (DataType base = current->self_type;;) {
			if ((r_type.kind == DataType::CLASS && base.kind == DataType::CLASS && base.class_type == r_type.class_type) ||
					(r_type.kind == DataType::NATIVE && ClassDB::is_parent_class(base.native_type, r_type.native_type))) {
				allowed = true;
				break;
			}
			if (base.kind != DataType::CLASS) {
				break;
			}
			base = base.class_type->base_type;
		}
	}
	if (!allowed) {
		push_error("super_as requires the current class, an ancestor class or an implemented interface.", p_call);
		return false;
	}
	if (interface) {
		const DataType *provider = r_type.kind == DataType::CLASS ? r_type.class_type->wgodot_default_methods.getptr(p_call->function_name) : nullptr;
		if (provider) {
			r_type = *provider;
		} else if (r_type.kind != DataType::NATIVE || !WGodotNativeInterfaces::get_descriptor(r_type.native_type)->default_methods.has(p_call->function_name)) {
			push_error(vformat("Interface '%s' has no default body for '%s'.", r_type.to_string(), p_call->function_name), p_call);
			return false;
		}
	}
	p_call->wgodot_super_type->resolved_type = r_type;
	return true;
}
