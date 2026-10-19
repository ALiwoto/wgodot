// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/variant/variant_parser.h"

// Resource codecs own their grammar and native construction. The text loader
// and C++ emitter dispatch through this interface, without class/field cases.
class WResourceCodec {
public:
	class NativeWriter {
	public:
		virtual String value(const Variant &p_value) = 0;
		virtual String symbol() = 0;
		virtual void include(const String &p_header) = 0;
		virtual void line(const String &p_line) = 0;
		virtual ~NativeWriter() = default;
	};

	class Assignment : public VariantParser::AssignmentParser {
		Resource *resource;
		const WResourceCodec *codec;

	public:
		Assignment(Resource *p_resource, const StringName &p_type, bool p_assign = true);
		virtual Error parse(const String &p_name, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) override;
	};

	static void register_codec(const StringName &p_type, const WResourceCodec *p_codec);
	static void unregister_codec(const StringName &p_type);
	static const WResourceCodec *find(const StringName &p_type);

	virtual bool owns_property(const StringName &p_name) const = 0;
	virtual bool owns_assignment(const String &p_name) const = 0;
	virtual Error read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) const = 0;
	virtual Error write(const Resource *p_resource, String &r_text, VariantWriter::EncodeResourceFunc p_encode, void *p_userdata) const = 0;
	virtual Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const = 0;
	virtual ~WResourceCodec() = default;
};
