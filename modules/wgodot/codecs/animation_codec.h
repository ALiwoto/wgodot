// wgodot-changes::file
#pragma once

#include "resource_codec.h"
#include "w_animation.h"

// WTracks v2: (version, path prefix, scalar constants, clocks, track records).
// v1 remains readable while existing project files await format review.
// Constants use c_<base36 ID> = literal; references decode to independent keys.
// d means the type's default value; c(...) repeats one value at all key times.
// Record: (Animation::TrackType, path, interpolation, clock index, payload,
//          optional {flags, update, audio_blend, transitions, handles}).
// Flags: disabled=1, imported=2, no loop wrap=4. Scalars in clocks and
// transform payloads are float32 unless explicitly introduced with f64.
// Values are tuples, not Resources or Variant arrays of numeric components.
class WAnimationCodec : public WResourceCodec {
public:
	virtual bool owns_property(const StringName &p_name) const override;
	virtual bool owns_assignment(const String &p_name) const override;
	virtual Error read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) const override;
	virtual Error write(const Resource *p_resource, String &r_text, VariantWriter::EncodeResourceFunc p_encode, void *p_userdata) const override;
	virtual Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override;
};

void register_animation_codec();
void unregister_animation_codec();
