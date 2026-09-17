// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::is_packed(const Parser::DataType &p_type) const {
	return !p_type.is_coroutine && p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY;
}

WGodotCppEmitter::Value WGodotCppEmitter::packed_array(const Parser::ExpressionNode *p_source, const Parser::DataType &p_target) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	Value result;
	if (p_source->type == Parser::Node::ARRAY) {
		// A literal constructs a new array; no existing WArray alias crosses the
		// boundary. Let Godot perform its packed element conversions.
		Parser::DataType array;
		array.kind = Parser::DataType::BUILTIN;
		array.builtin_type = Variant::ARRAY;
		result = array_literal(static_cast<const Parser::ArrayNode *>(p_source), array);
		result.code = "WGodotNative::construct<" + type(p_target, p_source) + ", " + variant_type(p_target.builtin_type) + ">(" + result.code + ")";
	} else {
		result = lower(p_source);
		result.code = "WGodotNative::copy_packed<" + type(p_target, p_source) + ">(" + result.code + ")";
	}
	result.cpp_type = type(p_target, p_source);
	result.borrowed = false;
	result.effects = true;
	return result;
}
