// wgodot-changes::file
#include "resource_codec.h"

#include "core/templates/hash_map.h"

static HashMap<StringName, const WResourceCodec *> codecs;

void WResourceCodec::register_codec(const StringName &p_type, const WResourceCodec *p_codec) {
	ERR_FAIL_COND(codecs.has(p_type));
	codecs.insert(p_type, p_codec);
}

void WResourceCodec::unregister_codec(const StringName &p_type) {
	codecs.erase(p_type);
}

const WResourceCodec *WResourceCodec::find(const StringName &p_type) {
	const WResourceCodec *const *codec = codecs.getptr(p_type);
	return codec ? *codec : nullptr;
}

WResourceCodec::Assignment::Assignment(Resource *p_resource, const StringName &p_type, bool p_assign) :
		resource(p_assign ? p_resource : nullptr), codec(find(p_type)) {}

Error WResourceCodec::Assignment::parse(const String &p_name, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) {
	handled = codec && codec->owns_assignment(p_name);
	return handled ? codec->read(resource, p_token, p_stream, r_line, r_error, p_resources) : OK;
}
