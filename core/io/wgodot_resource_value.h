// wgodot-changes::file
#pragma once

#include "wgodot_named_types.h"
#include "wgodot_native_resource_fields.h"

#include "core/variant/variant_parser.h"
#include "core/templates/hash_set.h"

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
	static bool native_field(const StringName &p_class, const StringName &p_name, WGodotResourceDataType &r_type, Variant::Type p_literal_type = Variant::NIL);
	static WGodotResourceDataType packed_element(Variant::Type p_type);
	static int component_width(Variant::Type p_type);
};

// Resolves omitted value/container constructors from declarations, without executing
// property getters or constructing nodes just to discover their field types.
class WGodotResourceValueParser : public VariantParser::AssignmentParser {
public:
	class FieldResolver {
	public:
		virtual Error field(const String &p_name, WGodotResourceDataType &r_type, bool &r_found, String &r_error) = 0;
		virtual ~FieldResolver() = default;
	};

private:
	using Type = WGodotResourceDataType;
	FieldResolver *field_resolver = nullptr;
	HashMap<StringName, Type> fields;
	StringName native_type;
	WGodotNativeResourceFields::Dependency declaration_source;
	HashSet<StringName> dependent_fields;
	WGodotNamedTypes::Resolver &resolver;
	VariantParser::AssignmentParser *custom;
	Object *initializing = nullptr;
	bool declarations_required = true;
	String base_directory;
	void resolve_paths(Variant &r_value, const Type &p_type) const;
	Error value(VariantParser::Token &p_token, Variant &r_value, const Type &p_type, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources, int p_depth = 0);
	Error value_data(VariantParser::Token &p_token, Variant &r_value, const Type &p_type, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources, int p_depth);
	Ref<Script> element_script(const Type &p_type);

public:
	static Error infer_constructor(VariantParser::Token &r_token, const Type &p_type, VariantParser::Stream *p_stream, String &r_error);
	static Error parse_repeat_count(VariantParser::Stream *p_stream, int &r_count, int &r_line, String &r_error);
	WGodotResourceValueParser(const String &p_path, const StringName &p_native, const Ref<Script> &p_script, WGodotNamedTypes::Resolver &p_resolver, VariantParser::AssignmentParser *p_custom = nullptr);
	void initialize_owned(Object *p_resource) { initializing = p_resource; }
	void require_declarations(bool p_required) { declarations_required = p_required; }
	void set_field_resolver(FieldResolver *p_resolver) { field_resolver = p_resolver; }
	void set_node_paths(const HashSet<StringName> &p_properties);
	Error parse(const String &p_name, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
	Error parse_value(const String &p_name, VariantParser::Token &p_token, Variant &r_value, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
};
