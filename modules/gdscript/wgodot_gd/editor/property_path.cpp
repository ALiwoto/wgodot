// wgodot-changes::file

#include "property_path.h"

#include "core/object/class_db.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/wgodot_gd/interface_helpers.h"
#include "modules/gdscript/wgodot_gd/script_resolution.h"

bool GDScriptAnalyzer::wgodot_resolve_property_path_segment(WGodotGDScriptPropertyPath::Segment &r_segment, const GDScriptParser::Node *p_source) {
	using Parser = GDScriptParser;
	const Parser::DataType &base = r_segment.base_type;
	if (!base.is_hard_type() || base.is_variant() || base.is_meta_type) {
		return false;
	}

	if (base.kind == Parser::DataType::BUILTIN) {
		if (!Variant::has_member(base.builtin_type, r_segment.name)) {
			return false;
		}
		r_segment.datatype = type_from_property(PropertyInfo(Variant::get_member_type(base.builtin_type, r_segment.name), r_segment.name), false, p_source);
		return true;
	}

	for (Parser::ClassNode *owner = base.class_type; owner != nullptr; owner = owner->base_type.class_type) {
		resolve_class_inheritance(owner, p_source);
		StringName name = r_segment.name;
		if (!owner->has_member(name) && WGodotGDScriptResolution::is_export_analysis()) {
			// Export text escapes binary identifiers; property paths contain their runtime names.
			name = StringName("${{" + String(name) + "}}");
		}
		if (!owner->has_member(name)) {
			continue;
		}
		resolve_class_member(owner, name, p_source);
		const Parser::ClassNode::Member member = owner->get_member(name);
		if (member.type != Parser::ClassNode::Member::VARIABLE && member.type != Parser::ClassNode::Member::CONSTANT) {
			return false;
		}
		r_segment.name = name;
		r_segment.owner = owner;
		r_segment.member = member;
		r_segment.datatype = member.get_datatype();
		if (member.type == Parser::ClassNode::Member::VARIABLE) {
			member.variable->usages++;
		} else {
			member.constant->usages++;
		}
		return true;
	}

	if (base.kind == Parser::DataType::SCRIPT && base.script_type.is_valid()) {
		List<PropertyInfo> properties;
		base.script_type->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.name == r_segment.name) {
				r_segment.datatype = type_from_property(property, false, p_source);
				return true;
			}
		}
	}

	// Interfaces declare their own properties separately from ClassDB. Their
	// required Object base supplies inherited properties such as Control.position.
	if (const auto *contract = WGodotGDScriptInterfaceHelpers::native_interface_for_member(base, r_segment.name)) {
		if (const auto *property = contract->properties.getptr(r_segment.name)) {
			r_segment.datatype = type_from_property(property->info, false, p_source);
			r_segment.datatype.is_read_only = property->setter.is_empty();
			return true;
		}
	}
	StringName native = base.native_type;
	if (const auto *contract = WGodotNativeInterfaces::get_descriptor(native)) {
		native = contract->native_base;
	}
	PropertyInfo property;
	if (ClassDB::get_property_info(native, r_segment.name, &property)) {
		r_segment.datatype = type_from_property(property, false, p_source);
		r_segment.datatype.is_read_only = ClassDB::get_property_setter(native, r_segment.name).is_empty();
		return true;
	}
	return false;
}

void GDScriptAnalyzer::wgodot_analyze_tween_property_call(const GDScriptParser::DataType &p_base_type, const GDScriptParser::CallNode *p_call) {
	using Parser = GDScriptParser;
	if (p_call->function_name != SNAME("tween_property") || p_call->arguments.size() != 4 || !ClassDB::is_parent_class(p_base_type.native_type, SNAME("Tween"))) {
		return;
	}

	const bool strict = wgodot_strict_type_checking_enabled();
	if (!strict && !WGodotGDScriptResolution::is_export_analysis()) {
		return;
	}
	const Parser::ExpressionNode *path_argument = p_call->arguments[1];
	const Variant::Type path_type = path_argument->reduced_value.get_type();
	if (!path_argument->is_constant || (path_type != Variant::STRING && path_type != Variant::NODE_PATH)) {
		if (strict) {
			push_error(R"*(Strict type checking requires tween_property() to use a constant property path so each property can be checked. Use a string or NodePath literal, or a constant.)*", path_argument);
		}
		return;
	}

	WGodotGDScriptPropertyPath resolved;
	resolved.path = path_argument->reduced_value;
	const Vector<StringName> names = resolved.path.get_as_property_path().get_subnames();
	if (names.is_empty()) {
		if (strict) {
			push_error(R"*(Strict type checking requires tween_property() to use a non-empty property path.)*", path_argument);
		}
		return;
	}

	Parser::DataType type = p_call->arguments[0]->type_constraint;
	String error;
	for (const StringName &name : names) {
		WGodotGDScriptPropertyPath::Segment segment;
		segment.name = name;
		segment.base_type = type;
		if (!wgodot_resolve_property_path_segment(segment, path_argument)) {
			error = vformat(R"*(Strict type checking cannot resolve property "%s" on type "%s" in tween_property() path "%s". Each property must exist on a statically known type.)*", name, type.to_string(), resolved.path);
			break;
		}
		resolved.segments.push_back(segment);
		type = segment.datatype;
		if (!type.is_hard_type() || wgodot_datatype_contains_variant(type)) {
			error = vformat(R"*(Strict type checking requires property "%s" in tween_property() path "%s" to have a fully known, static non-Variant type.)*", name, resolved.path);
			break;
		}
	}

	// Retain resolved references for export even when strict checking is disabled.
	// An unresolved suffix must not prevent renaming the known prefix of a path.
	wgodot_tween_property_paths.insert(p_call, resolved);
	if (!strict) {
		return;
	}
	if (!error.is_empty()) {
		push_error(error, path_argument);
		return;
	}

	for (const WGodotGDScriptPropertyPath::Segment &segment : resolved.segments) {
		if (segment.owner != nullptr) {
			wgodot_validate_private_member_access(segment.member, segment.owner, path_argument);
			wgodot_validate_protected_member_access(segment.member, segment.owner, path_argument);
		}
	}
	// Object::set_indexed writes every segment back, including object references.
	for (int i = resolved.segments.size() - 1; i >= 0; i--) {
		const WGodotGDScriptPropertyPath::Segment &segment = resolved.segments[i];
		const bool readonly_variable = i == resolved.segments.size() - 1 && segment.member.type == Parser::ClassNode::Member::VARIABLE && segment.member.variable->wgodot_readonly;
		if (segment.datatype.is_constant || segment.datatype.is_read_only || readonly_variable) {
			push_error(vformat(R"*(Strict type checking cannot tween read-only property "%s" in path "%s".)*", segment.name, resolved.path), path_argument);
			return;
		}
	}

	const Parser::DataType &value_type = p_call->arguments[2]->type_constraint;
	if (!value_type.is_hard_type() || wgodot_datatype_contains_variant(value_type)) {
		push_error(R"*(Strict type checking requires tween_property()'s final value to have a fully known, static non-Variant type.)*", p_call->arguments[2]);
		return;
	}
	// PropertyTweener accepts matching Variant types and conversions between int and float.
	const bool numeric = (type.builtin_type == Variant::INT || type.builtin_type == Variant::FLOAT) && (value_type.builtin_type == Variant::INT || value_type.builtin_type == Variant::FLOAT);
	if (!numeric && (type.builtin_type != value_type.builtin_type || !is_type_compatible(type, value_type, true))) {
		push_error(vformat(R"*(Strict type checking cannot tween property path "%s" of type "%s" to a final value of type "%s".)*", resolved.path, type.to_string(), value_type.to_string()), p_call->arguments[2]);
	}
}
