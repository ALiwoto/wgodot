// wgodot-changes::file
#pragma once
#include "wgodot_resource_snapshot.h"

#include "core/io/missing_resource.h"
#include "core/io/wgodot_resource_properties.h"

// A text resource graph without ScriptInstances or dependency loads. SubResource
// references retain identity; schema validation supplies all nominal typing.
class WGodotResourceDocument : public WGodotResourceValueParser::FieldResolver, public WGodotResourceProtobuf::Context {
	const WGodotResourceSnapshot &snapshot;
	const Dictionary &resource_paths;
	const Dictionary &compiled_resources;
	String source;
	String source_revision;
	Ref<MissingResource> root;
	Ref<MissingResource> current;
	HashMap<String, Ref<MissingResource>> external;
	HashMap<String, Ref<MissingResource>> internal;
	HashMap<Resource *, uint64_t> identities;
	Vector<Ref<MissingResource>> graph;
	WGodotResourceProperties properties;
	WGodotResourceUIDs uids;
	Dictionary scripts;
	Error declare(const StringName &p_name);
	Error validate(const Variant &p_value, const WGodotResourceEncode::Type &p_type, String &r_error);
	Error parse(String &r_error, int &r_line);
	static Error reference(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error, bool p_external);
	static Error external_reference(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error);
	static Error internal_reference(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error);
	static Error direct_reference(void *p_self, VariantParser::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error);
	Error field(const String &p_name, WGodotResourceDataType &r_type, bool &r_found, String &r_error) override;
	Ref<Resource> resolve(const WGodotResourceProtobuf::Reference &) override { return {}; }
	WGodotResourceProtobuf::Reference identify(const Ref<Resource> &p_resource) override;

public:
	WGodotResourceDocument(const WGodotResourceSnapshot &p_snapshot, const Dictionary &p_paths, const Dictionary &p_compiled) :
			snapshot(p_snapshot), resource_paths(p_paths), compiled_resources(p_compiled) {}
	Error load(const String &p_path, String &r_error);
	Error encode(const String &p_destination, String &r_error);
	Error cache() const;
};
