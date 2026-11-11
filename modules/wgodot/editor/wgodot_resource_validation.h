// wgodot-changes::file
#pragma once

#include "wgodot_resource_syntax.h"
#include "wgodot_resource_types.h"

#include "core/variant/variant_parser.h"

// One instance per file. Only the declaration snapshot is shared with other
// workers; references, parser state and dependency tracking belong to this file.
class WGodotResourceValidation : public VariantParser::AssignmentParser {
	using Type = WGodotResourceTypes::Type;
	using Qualification = WGodotResourceTypes::Qualification;
	using Token = VariantParser::Token;
	using Stream = VariantParser::Stream;
	using TokenType = VariantParser::TokenType;
	struct Reference {
		String path;
		StringName type;
	};
	struct NodeBinding {
		String scene;
		String owner;
		String target;
		StringName type;
		int line = 0;
	};
	String path;
	const WGodotResourceTypes *types;
	WGodotResourceSyntax::Result &result;
	HashMap<String, Reference> external;
	HashMap<String, Reference> internal;
	HashMap<String, StringName> resource_headers;
	HashMap<String, WGodotResourceSyntax::Result> scenes;
	HashSet<String> owned_scene_stack;
	HashSet<String> *scene_stack;
	HashSet<String> assignments;
	HashSet<String> declared_nodes;
	HashMap<StringName, String> global_paths;
	Vector<NodeBinding> node_bindings;
	StringName owner_type;
	String owner_node;
	bool node_section = false;
	bool nested_scene = false;
	String nested_scene_id;
	HashMap<String, HashMap<String, StringName>> nested_nodes;
	String current_property;
	String resource_section;
	bool collect_properties;

	Error fail(const String &p_message, String &r_error) const;
	Error token(Stream *p_stream, Token &r_token, int &r_line, String &r_error) const;
	Error expect(Stream *p_stream, TokenType p_type, int &r_line, String &r_error) const;
	char32_t peek(Stream *p_stream, int &r_line) const;
	Error annotation(Token p_token, Stream *p_stream, Type &r_type, int &r_line, String &r_error);
	Error value(Token p_token, Stream *p_stream, const Type &p_expected, const Qualification &p_qualification, Type &r_actual, int &r_line, String &r_error, int p_depth = 0);
	Error container(Stream *p_stream, const Type &p_type, const Qualification &p_qualification, int &r_line, String &r_error, int p_depth);
	Error reference(Stream *p_stream, const String &p_kind, Type &r_type, Qualification &r_qualification, int &r_line, String &r_error);
	Error field(const StringName &p_class, const StringName &p_name, Type &r_type, Qualification &r_qualification, String &r_error);
	Error resource_type(const String &p_path, StringName &r_type, String &r_error);
	Error instance(const Reference &p_reference, const String &p_prefix, String &r_error);
	Error class_type(const StringName &p_type, const StringName &p_base, String &r_error);
	Error literal(const Variant &p_value, const Type &p_expected, const Qualification &p_qualification, String &r_error);
	Error connection(const VariantParser::Tag &p_tag, String &r_error);
	Error tag_fields(const VariantParser::Tag &p_tag, String &r_error);
	static Error parse_external(void *p_self, Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error);
	static Error parse_internal(void *p_self, Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error);
	Error marker(Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error, bool p_external);

public:
	WGodotResourceValidation(const String &p_path, const WGodotResourceTypes *p_types, WGodotResourceSyntax::Result &r_result, HashSet<String> *p_scene_stack, bool p_collect_properties);
	void configure(VariantParser::ResourceParser &r_resources);
	Error header(const StringName &p_type, String &r_error);
	Error tag(const VariantParser::Tag &p_tag, const StringName &p_resource_type, String &r_error);
	Error finish(String &r_error);
	Error parse(const String &p_name, Token &p_token, Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
};
