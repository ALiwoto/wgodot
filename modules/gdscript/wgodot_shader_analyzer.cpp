// wgodot-changes::file
#include "gdscript_analyzer.h"

#include "core/object/class_db.h"

namespace {
String shader_parameter_suffix(const GDScriptParser::DataType &p_type, bool p_setter) {
	using Type = GDScriptParser::DataType;
	if (p_type.is_meta_type || !p_type.is_hard_type()) {
		return String();
	}
	if (p_type.kind == Type::BUILTIN || p_type.kind == Type::ENUM) {
		switch (p_type.builtin_type) {
			case Variant::BOOL:
				return "bool";
			case Variant::INT:
				return "int";
			case Variant::FLOAT:
				return "float";
			case Variant::VECTOR2:
				return "vector2";
			case Variant::VECTOR3:
				return "vector3";
			case Variant::VECTOR4:
				return "vector4";
			case Variant::VECTOR2I:
				return "vector2i";
			case Variant::VECTOR3I:
				return "vector3i";
			case Variant::VECTOR4I:
				return "vector4i";
			case Variant::COLOR:
				return "color";
			case Variant::TRANSFORM2D:
				return "transform2d";
			case Variant::BASIS:
				return "basis";
			case Variant::TRANSFORM3D:
				return "transform3d";
			case Variant::PROJECTION:
				return "projection";
			case Variant::PACKED_FLOAT32_ARRAY:
				return "float32_array";
			case Variant::PACKED_FLOAT64_ARRAY:
				return "float64_array";
			case Variant::PACKED_INT32_ARRAY:
				return "int32_array";
			case Variant::PACKED_VECTOR2_ARRAY:
				return "vector2_array";
			case Variant::PACKED_VECTOR3_ARRAY:
				return "vector3_array";
			case Variant::PACKED_VECTOR4_ARRAY:
				return "vector4_array";
			case Variant::PACKED_COLOR_ARRAY:
				return "color_array";
			default:
				return String();
		}
	}
	if (ClassDB::is_parent_class(p_type.native_type, SNAME("Texture"))) {
		if (p_setter || p_type.native_type == SNAME("Texture")) {
			return "texture";
		}
		if (p_type.native_type == SNAME("Texture2D")) {
			return "texture2d";
		}
		if (p_type.native_type == SNAME("Texture3D")) {
			return "texture3d";
		}
		if (p_type.native_type == SNAME("TextureLayered")) {
			return "texture_layered";
		}
	}
	return String();
}

void rename_call(GDScriptParser::CallNode *p_call, const StringName &p_name) {
	p_call->function_name = p_name;
	if (!p_call->callee) {
		return;
	}
	if (p_call->callee->type == GDScriptParser::Node::IDENTIFIER) {
		static_cast<GDScriptParser::IdentifierNode *>(p_call->callee)->name = p_name;
	} else if (p_call->callee->type == GDScriptParser::Node::SUBSCRIPT) {
		static_cast<GDScriptParser::SubscriptNode *>(p_call->callee)->attribute->name = p_name;
	}
}
} // namespace

bool GDScriptAnalyzer::wgodot_lower_shader_parameter_call(const GDScriptParser::DataType &p_base, GDScriptParser::CallNode *p_call) {
	if (!ClassDB::is_parent_class(p_base.native_type, SNAME("ShaderMaterial"))) {
		return true;
	}
	if (p_call->function_name == SNAME("get_shader_parameter")) {
		push_error("get_shader_parameter() is prohibited. Use get_shader_parameter_as(Type, name), for example get_shader_parameter_as(float, &\"strength\"). Its result already has the requested type.", p_call);
		return false;
	}
	const bool getter = p_call->function_name == SNAME("get_shader_parameter_as");
	const bool setter = p_call->function_name == SNAME("set_shader_parameter");
	if (!getter && !setter) {
		return true;
	}
	if (p_call->arguments.size() != 2) {
		push_error(getter ? "get_shader_parameter_as() requires a type and a parameter name." : "set_shader_parameter() requires a parameter name and a typed value.", p_call);
		return false;
	}
	const auto *value = p_call->arguments[getter ? 0 : 1];
	auto type = value->type_constraint;
	if (getter) {
		if (!type.is_meta_type || !type.is_constant) {
			push_error("get_shader_parameter_as() requires a statically known type as its first argument.", value);
			return false;
		}
		type = type_from_metatype(type);
	} else if (value->is_constant && value->reduced_value.get_type() == Variant::NIL) {
		p_call->arguments.remove_at(1);
		rename_call(p_call, SNAME("clear_shader_parameter"));
		return true;
	}
	const String suffix = shader_parameter_suffix(type, setter);
	if (suffix.is_empty()) {
		push_error(vformat("Shader parameter access requires a concrete supported shader value type, got '%s'. Use a scalar, vector, Color, matrix, packed numeric array or Texture type; convert dynamic values at their input boundary.", type.to_string()), value);
		return false;
	}
	if (getter) {
		p_call->arguments.remove_at(0);
	}
	// Normal ClassDB signature resolution now handles both the VM and gd2cpp.
	// The type token is compile-time syntax, not a runtime Variant argument.
	rename_call(p_call, String(getter ? "get_shader_parameter_" : "set_shader_parameter_") + suffix);
	return true;
}
