// wgodot-changes::file
#include "gdscript_analyzer.h"

using Parser = GDScriptParser;

namespace {
bool object_type(const Parser::DataType &p_type) {
	if (p_type.is_meta_type || !p_type.is_hard_type()) {
		return false;
	}
	if (p_type.kind == Parser::DataType::CLASS) {
		return !p_type.class_type->wgodot_record && !p_type.class_type->wgodot_static_class;
	}
	return p_type.kind == Parser::DataType::NATIVE || p_type.kind == Parser::DataType::SCRIPT;
}

Parser::DataType weak_type(const Parser::DataType &p_target) {
	Parser::DataType result;
	result.kind = Parser::DataType::NATIVE;
	result.builtin_type = Variant::OBJECT;
	result.native_type = SNAME("WeakRef");
	result.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
	result.wgodot_weak_ref = true;
	result.set_container_element_type(0, p_target);
	return result;
}
} // namespace

Parser::DataType GDScriptAnalyzer::wgodot_resolve_weak_ref(Parser::TypeNode *p_type) {
	if (p_type->type_chain.size() != 1 || p_type->container_types.size() != 1) {
		push_error("WeakRef requires one object target type: WeakRef[Target]. Construct it with weakref(value).", p_type);
		return Parser::DataType();
	}
	Parser::DataType target = type_from_metatype(resolve_datatype(p_type->container_types[0]));
	if (!object_type(target)) {
		push_error("WeakRef targets must be object classes or interfaces, with their immutable qualification preserved.", p_type);
		return Parser::DataType();
	}
	target.is_constant = false;
	return weak_type(target);
}

void GDScriptAnalyzer::wgodot_reduce_weak_ref(Parser::CallNode *p_call) {
	if (p_call->arguments.size() != 1) {
		push_error("weakref() requires one statically typed object argument.", p_call);
		return;
	}
	Parser::DataType target = p_call->arguments[0]->type_constraint;
	if (!object_type(target)) {
		push_error("weakref() requires a statically typed object; untyped values, class objects and value records cannot establish a weak target contract.", p_call);
		return;
	}
	target.is_constant = false;
	p_call->type_constraint = weak_type(target);
	p_call->wgodot_fresh_value = true;
	p_call->wgodot_native_call = true;
}

bool GDScriptAnalyzer::wgodot_weak_ref_compatible(const Parser::DataType &p_target, const Parser::DataType &p_source, const Parser::Node *p_source_node) {
	if (!p_target.wgodot_weak_ref) {
		return false; // Do not erase the target contract through Object or Variant.
	}
	if (p_source.kind == Parser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		return true;
	}
	if (!p_source.wgodot_weak_ref) {
		return false;
	}
	return wgodot_immutable_types_match(p_target, p_source) &&
			check_type_compatibility(p_target.get_container_element_type(0), p_source.get_container_element_type(0), false, p_source_node);
}
