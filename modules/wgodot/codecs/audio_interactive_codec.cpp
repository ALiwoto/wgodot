// wgodot-changes::file
#include "audio_interactive_codec.h"

#include "modules/modules_enabled.gen.h"
#ifdef MODULE_INTERACTIVE_MUSIC_ENABLED
#include "resource_codec.h"

#include "modules/interactive_music/audio_stream_interactive.h"

namespace {
using Stream = AudioStreamInteractive;

// This internal dictionary is a fixed record schema, not application Variant data.
Error read_transitions(Stream *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error) {
	Variant parsed;
	RETURN_IF_ERROR(VariantParser::parse_value(p_token, parsed, p_stream, r_line, r_error));
	if (parsed.get_type() != Variant::DICTIONARY) {
		r_error = "AudioStreamInteractive._transitions requires a dictionary.";
		return ERR_PARSE_ERROR;
	}
	const Dictionary transitions = parsed;
	for (const KeyValue<Variant, Variant> &entry : transitions) {
		if (entry.key.get_type() != Variant::VECTOR2I || entry.value.get_type() != Variant::DICTIONARY) {
			r_error = "Audio transitions require Vector2i clip pairs and transition records.";
			return ERR_PARSE_ERROR;
		}
		const Vector2i clips = entry.key;
		if (clips.x < Stream::CLIP_ANY || clips.y < Stream::CLIP_ANY ||
				(p_resource && (clips.x >= p_resource->get_clip_count() || clips.y >= p_resource->get_clip_count()))) {
			r_error = "Audio transition references a clip outside the stream.";
			return ERR_PARSE_ERROR;
		}
		const Dictionary record = entry.value;
		for (const KeyValue<Variant, Variant> &field : record) {
			if (field.key.get_type() != Variant::STRING && field.key.get_type() != Variant::STRING_NAME) {
				r_error = "Audio transition field names must be strings.";
				return ERR_PARSE_ERROR;
			}
			const String name = field.key;
			const Variant::Type type = field.value.get_type();
			bool valid = false;
			if (name == "from_time" || name == "to_time" || name == "fade_mode" || name == "filler_clip") {
				valid = type == Variant::INT;
			} else if (name == "fade_beats") {
				valid = type == Variant::FLOAT || type == Variant::INT;
			} else if (name == "use_filler_clip" || name == "hold_previous") {
				valid = type == Variant::BOOL;
			}
			if (!valid) {
				r_error = "Unknown or incorrectly typed audio transition field: " + name;
				return ERR_PARSE_ERROR;
			}
		}
		if (!record.has("from_time") || !record.has("to_time") || !record.has("fade_mode") || !record.has("fade_beats")) {
			r_error = "Audio transition requires from_time, to_time, fade_mode and fade_beats.";
			return ERR_PARSE_ERROR;
		}
		const int64_t from = record["from_time"];
		const int64_t to = record["to_time"];
		const int64_t fade = record["fade_mode"];
		if (from < 0 || from >= Stream::TRANSITION_FROM_TIME_MAX || to < 0 || to >= Stream::TRANSITION_TO_TIME_MAX || fade < 0 || fade >= Stream::FADE_MAX) {
			r_error = "Audio transition enum value is outside its declared range.";
			return ERR_PARSE_ERROR;
		}
		const bool use_filler = record.get("use_filler_clip", false);
		if (use_filler && !record.has("filler_clip")) {
			r_error = "Audio transition with use_filler_clip requires filler_clip.";
			return ERR_PARSE_ERROR;
		}
		const int64_t filler = record.get("filler_clip", 0);
		if (use_filler && (filler < 0 || (p_resource && filler >= p_resource->get_clip_count()))) {
			r_error = "Audio transition filler clip is outside the stream.";
			return ERR_PARSE_ERROR;
		}
		if (p_resource) {
			p_resource->add_transition(clips.x, clips.y, Stream::TransitionFromTime(from), Stream::TransitionToTime(to), Stream::FadeMode(fade), record["fade_beats"], use_filler, filler, record.get("hold_previous", false));
		}
	}
	return OK;
}

class AudioInteractiveCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &) const override { return false; }
	bool owns_native_property(const StringName &p_name) const override { return p_name == SNAME("_transitions"); }
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &p_name) const override { return p_name == "_transitions"; }
	Error read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *) const override {
		return read_transitions(Object::cast_to<Stream>(p_resource), p_token, p_stream, r_line, r_error);
	}
	Error check_types(VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *) const override {
		return read_transitions(nullptr, p_token, p_stream, r_line, r_error);
	}
	Error write(const Resource *, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		r_text = String();
		return OK;
	}
	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const Stream *stream = Object::cast_to<Stream>(p_resource);
		ERR_FAIL_NULL_V(stream, ERR_INVALID_DATA);
		p_writer.include("modules/interactive_music/audio_stream_interactive.h");
		const PackedInt32Array clips = stream->get_transition_list();
		for (int index = 0; index < clips.size(); index += 2) {
			const int from = clips[index];
			const int to = clips[index + 1];
			p_writer.line(p_target + "->add_transition(" + itos(from) + ", " + itos(to) +
					", AudioStreamInteractive::TransitionFromTime(" + itos(stream->get_transition_from_time(from, to)) +
					"), AudioStreamInteractive::TransitionToTime(" + itos(stream->get_transition_to_time(from, to)) +
					"), AudioStreamInteractive::FadeMode(" + itos(stream->get_transition_fade_mode(from, to)) +
					"), " + p_writer.value(stream->get_transition_fade_beats(from, to)) +
					", " + p_writer.value(stream->is_transition_using_filler_clip(from, to)) +
					", " + itos(stream->get_transition_filler_clip(from, to)) +
					", " + p_writer.value(stream->is_transition_holding_previous(from, to)) + ");");
		}
		return OK;
	}
};

AudioInteractiveCodec audio_codec;
} // namespace
#endif

void register_audio_interactive_codec() {
#ifdef MODULE_INTERACTIVE_MUSIC_ENABLED
	WResourceCodec::register_codec("AudioStreamInteractive", &audio_codec);
#endif
}

void unregister_audio_interactive_codec() {
#ifdef MODULE_INTERACTIVE_MUSIC_ENABLED
	WResourceCodec::unregister_codec("AudioStreamInteractive");
#endif
}
