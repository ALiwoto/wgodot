// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_stdlib.h"

using Parser = GDScriptParser;

namespace {
bool referenceable_class(const Parser::DataType &p_type) {
	if (p_type.kind == Parser::DataType::NATIVE) {
		return !WGodotGDScriptStdLib::has_global_interface(p_type.native_type);
	}
	if (p_type.kind != Parser::DataType::CLASS || p_type.class_type->wgodot_record || p_type.class_type->wgodot_static_class || p_type.class_type->wgodot_is_interface) {
		return false;
	}
	const Parser::ClassNode *root = p_type.class_type;
	while (root->outer) {
		root = root->outer;
	}
	return root->get_global_name() != StringName();
}
} // namespace

Parser::DataType GDScriptAnalyzer::wgodot_resolve_class_ref(Parser::TypeNode *p_type) {
	if (p_type->type_chain.size() != 1 || p_type->container_types.size() != 1) {
		push_error("ClassRef requires one object base type: ClassRef[Base].", p_type);
		return Parser::DataType();
	}
	Parser::DataType base = type_from_metatype(resolve_datatype(p_type->container_types[0]));
	if (!referenceable_class(base)) {
		push_error("ClassRef requires a native object class or a class within a script declaring class_name; records, static classes and interfaces are not class references.", p_type);
		return Parser::DataType();
	}
	Parser::DataType result;
	result.kind = Parser::DataType::BUILTIN;
	result.builtin_type = Variant::OBJECT;
	result.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
	result.wgodot_class_ref = true;
	base.is_constant = false;
	result.set_container_element_type(0, base);
	return result;
}

bool GDScriptAnalyzer::wgodot_class_ref_compatible(const Parser::DataType &p_target, const Parser::DataType &p_source, const Parser::Node *p_source_node) {
	if (!p_target.wgodot_class_ref) {
		return p_target.is_variant();
	}
	if (p_source.is_variant() || (p_source.kind == Parser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL)) {
		return true;
	}
	Parser::DataType source;
	if (p_source.wgodot_class_ref) {
		source = p_source.get_container_element_type(0);
	} else if (p_source.is_meta_type && (p_source.kind == Parser::DataType::CLASS || p_source.kind == Parser::DataType::NATIVE)) {
		if (!referenceable_class(p_source)) {
			return false;
		}
		source = type_from_metatype(p_source);
	} else {
		return false;
	}
	return check_type_compatibility(p_target.get_container_element_type(0), source, false, p_source_node);
}
