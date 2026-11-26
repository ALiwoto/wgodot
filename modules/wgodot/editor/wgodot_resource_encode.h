// wgodot-changes::file
#pragma once
#include "core/io/wgodot_resource_value.h"

#include "modules/gdscript/gdscript_parser.h"
#include "modules/wgodot/codecs/resource_protobuf.h"

#include <functional>

namespace WGodotResourceEncode {
// Owned wire declarations. Persist these with the generated codecs so data-only
// updates do not need a GDScript parser, compiler, or executing script instance.
struct Type {
	Variant::Type builtin_type = Variant::NIL;
	StringName name;
	Vector<Type> elements;
	Vector<StringName> members;
	bool float32 = false;
	bool wgodot_resource_path = false;
	bool wgodot_class_ref = false;
	bool value_container = false;
	Type() = default;
	explicit Type(const GDScriptParser::DataType &p_type);
	explicit Type(const Dictionary &p_data);
	Dictionary serialize() const;
	WGodotResourceDataType declaration() const;
	bool has_container_element_type(int p_index) const { return p_index < elements.size(); }
	const Type &get_container_element_type(int p_index) const { return elements[p_index]; }
};
// Variant exists only at the editor's decoded-source boundary. The schema
// selects a concrete protobuf encoding before any bytes are written.
Error field(WGodotResourceProtobuf::Writer &r_writer, uint32_t p_id, const Variant &p_value,
		const Type &p_type, WGodotResourceProtobuf::Context &p_context,
		const std::function<int64_t(const String &)> &p_path);
inline Error field(WGodotResourceProtobuf::Writer &r_writer, uint32_t p_id, const Variant &p_value,
		const GDScriptParser::DataType &p_type, WGodotResourceProtobuf::Context &p_context,
		const std::function<int64_t(const String &)> &p_path) {
	return field(r_writer, p_id, p_value, Type(p_type), p_context, p_path);
}
} //namespace WGodotResourceEncode
