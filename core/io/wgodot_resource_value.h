// wgodot-changes::file
#pragma once

#include "wgodot_named_types.h"

#include "core/variant/variant_parser.h"

// Owned declaration data; safe to share through a const snapshot.
struct WGodotResourceDataType {
	Variant::Type builtin = Variant::NIL;
	StringName name;
	Vector<WGodotResourceDataType> elements;
	bool resource_path = false;
	bool value_container = false;
	bool concrete() const;
	String text() const;
	static WGodotResourceDataType from_property(const PropertyInfo &p_info);
	static WGodotResourceDataType packed_element(Variant::Type p_type);
};

// Resolves omitted container constructors from declarations, without executing
// property getters or constructing nodes just to discover their field types.
class WGodotResourceValueParser : public VariantParser::AssignmentParser {
	using Type = WGodotResourceDataType;
	HashMap<StringName, Type> fields;
	WGodotNamedTypes::Resolver &resolver;
	VariantParser::AssignmentParser *custom;
	Object *initializing = nullptr;
	Error value(VariantParser::Token &p_token, Variant &r_value, const Type &p_type, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources, int p_depth = 0);
	Error value_data(VariantParser::Token &p_token, Variant &r_value, const Type &p_type, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources, int p_depth);
	Ref<Script> element_script(const Type &p_type);

public:
	static Error parse_repeat_count(VariantParser::Stream *p_stream, int &r_count, int &r_line, String &r_error);
	WGodotResourceValueParser(const StringName &p_native, const Ref<Script> &p_script, WGodotNamedTypes::Resolver &p_resolver, VariantParser::AssignmentParser *p_custom = nullptr);
	void initialize_owned(Object *p_resource) { initializing = p_resource; }
	Error parse(const String &p_name, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
	Error parse_value(const String &p_name, VariantParser::Token &p_token, Variant &r_value, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
};
