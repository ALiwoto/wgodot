// wgodot-changes::file
#include "animation_codec.h"
#include "animation_constants.h"
#include "animation_diagnostics.h"

#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
#include "core/os/os.h"
#endif

namespace {
using Parser = VariantParser;

class Reader {
	Parser::Stream *stream;
	int &line;
	String &error;
	Parser::ResourceParser *resources;
	Parser::Token token;
	bool pending = true;
	String prefix;
	Vector<Vector<double>> clocks;
	Vector<WAnimation::CodecConstant> constants;
	int version = 0;
	WAnimation *animation = nullptr;
	bool wide_values = false;
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
	WAnimationCodecProfile profile;
#endif

	Error peek() {
		if (!pending) {
			RETURN_IF_ERROR(Parser::get_token(stream, token, line, error));
			pending = true;
		}
		return OK;
	}

	Error fail(const String &p_message) {
		error = "WAnimation: " + p_message;
		return ERR_PARSE_ERROR;
	}

	Error expect(Parser::TokenType p_type) {
		RETURN_IF_ERROR(peek());
		if (token.type != p_type) {
			return fail("unexpected token while reading track data");
		}
		pending = false;
		return OK;
	}

	Error comma() { return expect(Parser::TK_COMMA); }

	Error number(double &r_value, bool p_wide = false, bool p_reference = true) {
		RETURN_IF_ERROR(peek());
		if (p_reference && token.type == Parser::TK_IDENTIFIER) {
			uint32_t id;
			if (!WAnimationConstants::parse_id(token.value, id) || id >= uint32_t(constants.size()) || !constants[id].defined) {
				return fail("undefined numeric constant: " + String(token.value));
			}
			r_value = constants[id].value;
			W_ANIMATION_CODEC_COUNT(profile, references, 1);
		} else if (token.type == Parser::TK_NUMBER) {
			r_value = token.value;
			W_ANIMATION_CODEC_COUNT(profile, literals, 1);
		} else {
			return fail("expected a finite number or numeric constant");
		}
		if (!p_wide) {
			r_value = double(float(r_value));
		}
		pending = false;
		return Math::is_finite(r_value) ? OK : fail("non-finite key component");
	}

	Error integer(int &r_value) {
		double value;
		RETURN_IF_ERROR(number(value, true, false));
		if (value < INT32_MIN || value > INT32_MAX || Math::floor(value) != value) {
			return fail("expected a 32-bit integer");
		}
		r_value = int(value);
		return OK;
	}

	Error string(String &r_value) {
		RETURN_IF_ERROR(peek());
		if (token.type != Parser::TK_STRING && token.type != Parser::TK_STRING_NAME) {
			return fail("expected a string");
		}
		r_value = token.value;
		pending = false;
		return OK;
	}

	Error variant(Variant &r_value) {
		RETURN_IF_ERROR(peek());
		pending = false;
		return Parser::parse_value(token, r_value, stream, line, error, resources);
	}

	Error components(double *r_values, int p_count) {
		for (int i = 0; i < p_count; i++) {
			if (i) {
				RETURN_IF_ERROR(comma());
			}
			RETURN_IF_ERROR(number(r_values[i], wide_values));
		}
		return OK;
	}

	Error read_value(Vector3 &r_value, const String &) {
		double values[3];
		RETURN_IF_ERROR(components(values, 3));
		r_value = Vector3(values[0], values[1], values[2]);
		return OK;
	}

	Error read_value(Quaternion &r_value, const String &) {
		double values[4];
		RETURN_IF_ERROR(components(values, 4));
		r_value = Quaternion(values[0], values[1], values[2], values[3]);
		return OK;
	}

	Error read_value(float &r_value, const String &) {
		double value;
		RETURN_IF_ERROR(number(value, wide_values));
		r_value = value;
		return OK;
	}

	Error read_value(StringName &r_value, const String &) {
		String value;
		RETURN_IF_ERROR(string(value));
		r_value = value;
		return OK;
	}

	Error read_value(WAnimation::BezierValue &r_value, const String &) {
		double values[5];
		RETURN_IF_ERROR(components(values, 5));
		r_value.value = values[0];
		r_value.in_handle = Vector2(values[1], values[2]);
		r_value.out_handle = Vector2(values[3], values[4]);
		return OK;
	}

	Error read_value(WAnimation::AudioValue &r_value, const String &) {
		Variant stream_value;
		RETURN_IF_ERROR(variant(stream_value));
		if (stream_value.get_type() != Variant::OBJECT && stream_value.get_type() != Variant::NIL) {
			return fail("audio stream must be a resource reference");
		}
		r_value.stream = stream_value;
		RETURN_IF_ERROR(peek());
		if (token.type == Parser::TK_COMMA) {
			pending = false;
			double start, end;
			RETURN_IF_ERROR(number(start, true));
			RETURN_IF_ERROR(comma());
			RETURN_IF_ERROR(number(end, true));
			r_value.start_offset = start;
			r_value.end_offset = end;
		}
		return OK;
	}

	Error read_value(Variant &r_value, const String &p_type) {
		if (p_type == "float") {
			double value;
			RETURN_IF_ERROR(number(value, true));
			r_value = value;
			return OK;
		}
		if (p_type == "vec2" || p_type == "vec3" || p_type == "vec4" || p_type == "quat" || p_type == "color" || p_type == "transform3d") {
			int count = p_type == "vec2" ? 2 : p_type == "vec3" ? 3 : p_type == "transform3d" ? 12 : 4;
			double values[12];
			bool previous = wide_values;
			wide_values = true; // Property values retain their original Variant precision.
			Error result = components(values, count);
			wide_values = previous;
			RETURN_IF_ERROR(result);
			if (p_type == "vec2") {
				r_value = Vector2(values[0], values[1]);
			} else if (p_type == "vec3") {
				r_value = Vector3(values[0], values[1], values[2]);
			} else if (p_type == "vec4") {
				r_value = Vector4(values[0], values[1], values[2], values[3]);
			} else if (p_type == "quat") {
				r_value = Quaternion(values[0], values[1], values[2], values[3]);
			} else if (p_type == "color") {
				r_value = Color(values[0], values[1], values[2], values[3]);
			} else {
				Transform3D transform;
				for (int row = 0; row < 3; row++) {
					transform.basis.rows[row] = Vector3(values[row * 3], values[row * 3 + 1], values[row * 3 + 2]);
				}
				transform.origin = Vector3(values[9], values[10], values[11]);
				r_value = transform;
			}
			return OK;
		}
		RETURN_IF_ERROR(variant(r_value));
		if (p_type == "variant") {
			return OK;
		}
		Variant::Type expected = Variant::NIL;
		if (p_type == "bool") {
			expected = Variant::BOOL;
		} else if (p_type == "int") {
			expected = Variant::INT;
		} else if (p_type == "string") {
			expected = Variant::STRING;
		} else if (p_type == "resource") {
			expected = Variant::OBJECT;
		} else if (p_type == "vector2_array") {
			expected = Variant::PACKED_VECTOR2_ARRAY;
		} else {
			return fail("unknown value type: " + p_type);
		}
		return r_value.get_type() == expected ? OK : fail("value differs from its declared type: " + p_type);
	}

	Error read_method(WAnimation::MethodKey &r_key) {
		Variant value;
		RETURN_IF_ERROR(variant(value));
		if (value.get_type() != Variant::DICTIONARY) {
			return fail("method key must contain method and args");
		}
		Dictionary fields = value;
		if (fields.size() != 2 || !fields.has("method") || !fields.has("args")) {
			return fail("invalid method key fields");
		}
		r_key.method = fields["method"];
		Array arguments = fields["args"];
		r_key.params.resize(arguments.size());
		for (int i = 0; i < arguments.size(); i++) {
			r_key.params.write[i] = arguments[i];
		}
		return OK;
	}

	template <typename T>
	Error key_value(T &r_key, const String &p_type) {
		return read_value(r_key.value, p_type);
	}
	Error key_value(WAnimation::MethodKey &r_key, const String &) { return read_method(r_key); }

	template <typename T>
	void set_default(T &, Animation::TrackType) {}
	void set_default(WAnimation::VectorKey &r_key, Animation::TrackType p_type) {
		r_key.value = p_type == Animation::TYPE_SCALE_3D ? Vector3(1, 1, 1) : Vector3();
	}
	void set_default(WAnimation::ScalarKey &r_key, Animation::TrackType) {
		r_key.value = 0;
	}

	template <typename T>
	Error handle_modes(LocalVector<T> &, const PackedInt32Array &) { return fail("handles apply only to Bezier tracks"); }
	Error handle_modes(LocalVector<WAnimation::BezierKey> &r_keys, const PackedInt32Array &p_modes) {
		if (p_modes.size() != int(r_keys.size())) {
			return fail("Bezier handle count differs from clock");
		}
		for (uint32_t i = 0; i < r_keys.size(); i++) {
			if (p_modes[i] < 0 || p_modes[i] > 3) {
				return fail("invalid Bezier handle mode");
			}
#ifdef TOOLS_ENABLED
			r_keys[i].value.handle_mode = Animation::HandleMode(p_modes[i]);
#endif
		}
		return OK;
	}

	template <typename T>
	Error track_keys(WAnimation::TrackSettings settings, const Vector<double> &p_times) {
		LocalVector<T> keys;
		keys.reserve(p_times.size());
		String payload_type;
		wide_values = false;
		RETURN_IF_ERROR(peek());
		if (token.type == Parser::TK_IDENTIFIER) {
			payload_type = token.value;
			pending = false;
			if (payload_type == "f64") {
				wide_values = true;
				RETURN_IF_ERROR(peek());
				payload_type = token.type == Parser::TK_IDENTIFIER ? String(token.value) : String();
				if (!payload_type.is_empty()) {
					pending = false;
				}
			}
		}
		if (payload_type == (version == 2 ? "d" : "default")) {
			if (settings.type < Animation::TYPE_POSITION_3D || settings.type > Animation::TYPE_BLEND_SHAPE) {
				return fail("default payload requires a transform or blend-shape track");
			}
			keys.resize(p_times.size());
			for (T &key : keys) {
				set_default(key, settings.type);
			}
		} else {
			RETURN_IF_ERROR(peek());
			bool constant = token.type == Parser::TK_PARENTHESIS_OPEN;
			if (settings.type != Animation::TYPE_VALUE && payload_type != (constant ? (version == 2 ? "c" : "constant") : "")) {
				return fail("expected constant(...) or a key array");
			}
			bool tuple = settings.type == Animation::TYPE_POSITION_3D || settings.type == Animation::TYPE_ROTATION_3D || settings.type == Animation::TYPE_SCALE_3D || settings.type == Animation::TYPE_BEZIER || settings.type == Animation::TYPE_AUDIO || payload_type == "vec2" || payload_type == "vec3" || payload_type == "vec4" || payload_type == "quat" || payload_type == "color" || payload_type == "transform3d";
			if (settings.type == Animation::TYPE_VALUE && payload_type.is_empty()) {
				return fail("property track requires a payload type");
			}
			RETURN_IF_ERROR(expect(constant ? Parser::TK_PARENTHESIS_OPEN : Parser::TK_BRACKET_OPEN));
			if (constant) {
				if (p_times.is_empty()) {
					return fail("constant payload requires at least one key time");
				}
				T key;
				RETURN_IF_ERROR(key_value(key, payload_type));
				RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_CLOSE));
				keys.resize(p_times.size());
				for (T &destination : keys) {
					destination = key;
				}
			} else {
				while (true) {
					RETURN_IF_ERROR(peek());
					if (token.type == Parser::TK_BRACKET_CLOSE) {
						pending = false;
						break;
					}
					if (tuple) {
						RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_OPEN));
					}
					T key;
					RETURN_IF_ERROR(key_value(key, payload_type));
					if (tuple) {
						RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_CLOSE));
					}
					keys.push_back(key);
					RETURN_IF_ERROR(peek());
					if (token.type != Parser::TK_BRACKET_CLOSE) {
						RETURN_IF_ERROR(comma());
					}
				}
			}
		}
		if (keys.size() != uint32_t(p_times.size())) {
			return fail("key count differs from its clock");
		}
		for (uint32_t i = 0; i < keys.size(); i++) {
			keys[i].time = p_times[i];
			if (settings.type == Animation::TYPE_BEZIER) {
				keys[i].transition = 0; // Animation's Bezier keys do not use easing.
			}
		}
		RETURN_IF_ERROR(peek());
		if (token.type == Parser::TK_COMMA) {
			pending = false;
			Variant value;
			RETURN_IF_ERROR(variant(value));
			if (value.get_type() != Variant::DICTIONARY) {
				return fail("track options must be a dictionary");
			}
			Dictionary options = value;
			for (const KeyValue<Variant, Variant> &option : options) {
				String name = option.key;
				if (name == "flags") {
					int flags = option.value;
					if (flags < 0 || flags > 7) {
						return fail("unknown track flags");
					}
					settings.enabled = !(flags & 1);
					settings.imported = flags & 2;
					settings.loop_wrap = !(flags & 4);
				} else if (name == "update" && settings.type == Animation::TYPE_VALUE) {
					int mode = option.value;
					if (mode < 0 || mode > Animation::UPDATE_CAPTURE) {
						return fail("invalid property update mode");
					}
					settings.update_mode = Animation::UpdateMode(mode);
				} else if (name == "audio_blend" && settings.type == Animation::TYPE_AUDIO) {
					settings.audio_blend = option.value;
				} else if (name == "transitions") {
					if (option.value.get_type() != Variant::PACKED_FLOAT32_ARRAY && option.value.get_type() != Variant::PACKED_FLOAT64_ARRAY) {
						return fail("transitions must be a packed float array");
					}
					PackedFloat64Array transitions = option.value;
					if (transitions.size() != int(keys.size())) {
						return fail("transition count differs from clock");
					}
					for (uint32_t i = 0; i < keys.size(); i++) {
						keys[i].transition = transitions[i];
					}
				} else if (name == "handles") {
					RETURN_IF_ERROR(handle_modes(keys, option.value));
				} else {
					return fail("unknown track option: " + name);
				}
			}
		}
		RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_CLOSE));
		W_ANIMATION_CODEC_COUNT(profile, tracks, 1);
		W_ANIMATION_CODEC_COUNT(profile, keys, keys.size());
		W_ANIMATION_CODEC_COUNT(profile, key_bytes, keys.size() * sizeof(T));
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
		const uint64_t attach_started = profile.active() ? OS::get_singleton()->get_ticks_usec() : 0;
#endif
		Error result = animation->set_keyframes(animation->append_track(settings), std::move(keys));
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
		if (profile.active()) {
			profile.attach_usec += OS::get_singleton()->get_ticks_usec() - attach_started;
		}
#endif
		return result;
	}

	Error constant_table() {
		RETURN_IF_ERROR(expect(Parser::TK_CURLY_BRACKET_OPEN));
		while (true) {
			RETURN_IF_ERROR(peek());
			if (token.type == Parser::TK_CURLY_BRACKET_CLOSE) {
				pending = false;
				break;
			}
			uint32_t id;
			if (token.type != Parser::TK_IDENTIFIER || !WAnimationConstants::parse_id(token.value, id)) {
				return fail("expected a base36 constant name such as c_1");
			}
			pending = false;
			RETURN_IF_ERROR(expect(Parser::TK_EQUAL));
			double value;
			RETURN_IF_ERROR(number(value, true, false));
			if (id >= uint32_t(constants.size())) {
				constants.resize(id + 1);
			} else if (constants[id].defined) {
				return fail("duplicate numeric constant: " + WAnimationConstants::name(id));
			}
			constants.write[id] = { value, true };
			W_ANIMATION_CODEC_COUNT(profile, constants, 1);
			RETURN_IF_ERROR(peek());
			if (token.type != Parser::TK_CURLY_BRACKET_CLOSE) {
				RETURN_IF_ERROR(comma());
			}
		}
		W_ANIMATION_CODEC_COUNT(profile, constant_table_bytes, constants.size() * sizeof(WAnimation::CodecConstant));
		return OK;
	}

	Error track() {
		RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_OPEN));
		int kind, interpolation, clock;
		RETURN_IF_ERROR(integer(kind));
		if (kind < Animation::TYPE_VALUE || kind > Animation::TYPE_ANIMATION) {
			return fail("unknown track type");
		}
		RETURN_IF_ERROR(comma());
		RETURN_IF_ERROR(peek());
		NodePath path;
		if (token.type == Parser::TK_STRING) {
			String text;
			RETURN_IF_ERROR(string(text));
			path = !prefix.is_empty() && text.begins_with(".") ? prefix + text.substr(1) : text;
		} else {
			Variant value;
			RETURN_IF_ERROR(variant(value));
			if (value.get_type() != Variant::NODE_PATH) {
				return fail("path must be a string or explicit NodePath");
			}
			path = value;
		}
		RETURN_IF_ERROR(comma());
		RETURN_IF_ERROR(integer(interpolation));
		if (interpolation < Animation::INTERPOLATION_NEAREST || interpolation > Animation::INTERPOLATION_LINEAR_NORMALIZED) {
			return fail("unknown interpolation mode");
		}
		RETURN_IF_ERROR(comma());
		RETURN_IF_ERROR(integer(clock));
		if (clock < 0 || clock >= clocks.size()) {
			return fail("clock index is outside the declared table");
		}
		RETURN_IF_ERROR(comma());
		WAnimation::TrackSettings settings;
		settings.type = Animation::TrackType(kind);
		settings.path = path;
		settings.interpolation = Animation::InterpolationType(interpolation);
		switch (settings.type) {
			case Animation::TYPE_VALUE: return track_keys<WAnimation::ValueKey>(settings, clocks[clock]);
			case Animation::TYPE_POSITION_3D:
			case Animation::TYPE_SCALE_3D: return track_keys<WAnimation::VectorKey>(settings, clocks[clock]);
			case Animation::TYPE_ROTATION_3D: return track_keys<WAnimation::RotationKey>(settings, clocks[clock]);
			case Animation::TYPE_BLEND_SHAPE: return track_keys<WAnimation::ScalarKey>(settings, clocks[clock]);
			case Animation::TYPE_METHOD: return track_keys<WAnimation::MethodKey>(settings, clocks[clock]);
			case Animation::TYPE_BEZIER: return track_keys<WAnimation::BezierKey>(settings, clocks[clock]);
			case Animation::TYPE_AUDIO: return track_keys<WAnimation::AudioKey>(settings, clocks[clock]);
			case Animation::TYPE_ANIMATION: return track_keys<WAnimation::AnimationKey>(settings, clocks[clock]);
		}
		return ERR_INVALID_DATA;
	}

public:
	Reader(Parser::Token p_token, Parser::Stream *p_stream, int &r_line, String &r_error, Parser::ResourceParser *p_resources) :
			stream(p_stream), line(r_line), error(r_error), resources(p_resources), token(p_token) {}

	void report(WAnimation *p_target, Error p_status) {
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
		profile.finish(p_target ? p_target->get_path() : String("<dependency scan>"), version, p_status);
#endif
	}

	Error read(WAnimation *p_target) {
		if (token.type != Parser::TK_IDENTIFIER || String(token.value) != "WTracks") {
			return fail("expected WTracks(version, prefix, clocks, tracks)");
		}
		pending = false;
		RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_OPEN));
		RETURN_IF_ERROR(integer(version));
		if (version != 1 && version != 2) {
			return fail("unsupported codec version: " + itos(version));
		}
		RETURN_IF_ERROR(comma());
		RETURN_IF_ERROR(string(prefix));
		RETURN_IF_ERROR(comma());
		if (version == 2) {
			RETURN_IF_ERROR(constant_table());
			RETURN_IF_ERROR(comma());
		}
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
		profile.phase(WAnimationCodecProfile::CLOCKS);
#endif
		RETURN_IF_ERROR(expect(Parser::TK_BRACKET_OPEN));
		while (true) {
			RETURN_IF_ERROR(peek());
			if (token.type == Parser::TK_BRACKET_CLOSE) {
				pending = false;
				break;
			}
			bool wide = token.type == Parser::TK_IDENTIFIER && String(token.value) == "f64";
			if (wide) {
				pending = false;
			}
			RETURN_IF_ERROR(expect(Parser::TK_BRACKET_OPEN));
			Vector<double> times;
			while (true) {
				RETURN_IF_ERROR(peek());
				if (token.type == Parser::TK_BRACKET_CLOSE) {
					pending = false;
					break;
				}
				double time;
				RETURN_IF_ERROR(number(time, wide));
				if (!times.is_empty() && time < times[times.size() - 1]) {
					return fail("clock times must be nondecreasing");
				}
				times.push_back(time);
				RETURN_IF_ERROR(peek());
				if (token.type != Parser::TK_BRACKET_CLOSE) {
					RETURN_IF_ERROR(comma());
				}
			}
			clocks.push_back(times);
			W_ANIMATION_CODEC_COUNT(profile, clock_values, times.size());
			RETURN_IF_ERROR(peek());
			if (token.type != Parser::TK_BRACKET_CLOSE) {
				RETURN_IF_ERROR(comma());
			}
		}
		RETURN_IF_ERROR(comma());
		RETURN_IF_ERROR(expect(Parser::TK_BRACKET_OPEN));
#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
		profile.phase(WAnimationCodecProfile::TRACKS);
#endif
		Ref<WAnimation> decoded;
		decoded.instantiate();
		animation = decoded.ptr();
		while (true) {
			RETURN_IF_ERROR(peek());
			if (token.type == Parser::TK_BRACKET_CLOSE) {
				pending = false;
				break;
			}
			RETURN_IF_ERROR(track());
			RETURN_IF_ERROR(peek());
			if (token.type != Parser::TK_BRACKET_CLOSE) {
				RETURN_IF_ERROR(comma());
			}
		}
		RETURN_IF_ERROR(expect(Parser::TK_PARENTHESIS_CLOSE));
#ifdef TOOLS_ENABLED
		decoded->set_codec_constants(constants);
#endif
		if (p_target) {
			p_target->replace_tracks(*decoded.ptr());
		}
		return OK;
	}
};
} // namespace

Error WAnimationCodec::read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *p_resources) const {
	WAnimation *animation = Object::cast_to<WAnimation>(p_resource);
	ERR_FAIL_COND_V(p_resource && !animation, ERR_INVALID_PARAMETER);
	Reader reader(p_token, p_stream, r_line, r_error, p_resources);
	Error result = reader.read(animation);
	reader.report(animation, result);
	return result;
}
