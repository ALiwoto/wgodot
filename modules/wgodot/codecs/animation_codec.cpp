// wgodot-changes::file
#include "animation_codec.h"

namespace {
WAnimationCodec animation_codec;
}

bool WAnimationCodec::owns_property(const StringName &p_name) const {
	return p_name == SNAME("_codec_constants") || String(p_name).begins_with("tracks/");
}

bool WAnimationCodec::owns_assignment(const String &p_name) const {
	return p_name == "tracks";
}

void register_animation_codec() {
	WResourceCodec::register_codec("WAnimation", &animation_codec);
}

void unregister_animation_codec() {
	WResourceCodec::unregister_codec("WAnimation");
}
