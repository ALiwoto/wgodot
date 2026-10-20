// wgodot-changes::file
#include "animation_codec.h"
#include "animation_constants.h"
#include "animation_numbers.h"

#include "core/string/string_builder.h"

#include <cstring>

namespace {
String quoted(const String &p_text) {
	return "\"" + p_text.c_escape() + "\"";
}

class Writer {
	const WAnimation &animation;
	VariantWriter::EncodeResourceFunc encode;
	void *userdata;
	struct Record {
		String opening;
		String payload;
		String options;
		bool numeric = false;
	};
	Vector<Record> records;
	WAnimationConstants constants;
	StringBuilder text;
	Vector<Vector<double>> clocks;
	Vector<int> track_clocks;
	String prefix;
	Error error = OK;

	String variant(const Variant &p_value) {
		String result;
		Error status = VariantWriter::write_to_string(p_value, result, true, encode, userdata);
		if (status != OK) {
			error = status;
		}
		return result;
	}

	template <typename T>
	String components(const T &p_value, int p_count, bool p_wide) {
		String result;
		for (int i = 0; i < p_count; i++) {
			if (i) {
				result += ", ";
			}
			result += p_wide ? w_animation_number(double(p_value[i])) : w_animation_number(float(p_value[i]));
		}
		return result;
	}

	String value(const Vector3 &p_value, bool p_wide) { return components(p_value, 3, p_wide); }
	String value(const Quaternion &p_value, bool p_wide) { return components(p_value, 4, p_wide); }
	String value(float p_value, bool) { return w_animation_number(p_value); }
	String value(const StringName &p_value, bool) { return quoted(p_value); }
	String value(const WAnimation::BezierValue &p_value, bool p_wide) {
		double numbers[] = { p_value.value, p_value.in_handle.x, p_value.in_handle.y, p_value.out_handle.x, p_value.out_handle.y };
		return components(numbers, 5, p_wide);
	}
	String value(const WAnimation::AudioValue &p_value, bool) {
		String result = variant(p_value.stream);
		if (p_value.start_offset != 0 || p_value.end_offset != 0) {
			result += ", " + w_animation_number(p_value.start_offset) + ", " + w_animation_number(p_value.end_offset);
		}
		return result;
	}
	String value(const Variant &p_value, bool) {
		// Property tracks really are Variants. Homogeneous numeric/vector tracks
		// declare their type once; resource callbacks retain external identities.
		switch (p_value.get_type()) {
			case Variant::FLOAT: return w_animation_number(double(p_value));
			case Variant::VECTOR2: return components(Vector2(p_value), 2, true);
			case Variant::VECTOR3: return components(Vector3(p_value), 3, true);
			case Variant::VECTOR4: return components(Vector4(p_value), 4, true);
			case Variant::QUATERNION: return components(Quaternion(p_value), 4, true);
			case Variant::COLOR: return components(Color(p_value), 4, false);
			case Variant::TRANSFORM3D: {
				Transform3D transform = p_value;
				return components(transform.basis.rows[0], 3, true) + ", " + components(transform.basis.rows[1], 3, true) + ", " + components(transform.basis.rows[2], 3, true) + ", " + components(transform.origin, 3, true);
			}
			default: return variant(p_value);
		}
	}

	template <typename T>
	String key_value(const T &p_key, bool p_wide, bool) { return value(p_key.value, p_wide); }
	String key_value(const WAnimation::ValueKey &p_key, bool p_wide, bool p_generic) {
		return p_generic ? variant(p_key.value) : value(p_key.value, p_wide);
	}
	String key_value(const WAnimation::MethodKey &p_key, bool, bool) {
		Array args;
		for (const Variant &argument : p_key.params) {
			args.push_back(argument);
		}
		return "{\"method\": " + quoted(p_key.method) + ", \"args\": " + variant(args) + "}";
	}

	String property_type(const LocalVector<WAnimation::ValueKey> &p_keys) {
		if (p_keys.is_empty()) {
			return "variant";
		}
		Variant::Type type = p_keys[0].value.get_type();
		for (const auto &key : p_keys) {
			if (key.value.get_type() != type) {
				return "variant";
			}
		}
		switch (type) {
			case Variant::BOOL: return "bool";
			case Variant::INT: return "int";
			case Variant::FLOAT: return "float";
			case Variant::STRING: return "string";
			case Variant::VECTOR2: return "vec2";
			case Variant::VECTOR3: return "vec3";
			case Variant::VECTOR4: return "vec4";
			case Variant::QUATERNION: return "quat";
			case Variant::COLOR: return "color";
			case Variant::TRANSFORM3D: return "transform3d";
			case Variant::PACKED_VECTOR2_ARRAY: return "vector2_array";
			case Variant::OBJECT: return "resource";
			default: return "variant";
		}
	}

	template <typename T>
	void handles(const LocalVector<T> &, Vector<String> &) {}
	void handles(const LocalVector<WAnimation::BezierKey> &p_keys, Vector<String> &r_options) {
#ifdef TOOLS_ENABLED
		String values;
		bool non_default = false;
		for (const auto &key : p_keys) {
			if (!values.is_empty()) {
				values += ", ";
			}
			values += itos(key.value.handle_mode);
			non_default |= key.value.handle_mode != Animation::HANDLE_MODE_FREE;
		}
		if (non_default) {
			r_options.push_back("\"handles\": PackedInt32Array(" + values + ")");
		}
#endif
	}

	template <typename T>
	void track(int p_track, const LocalVector<T> &p_keys, const String &p_type = String()) {
		const auto settings = animation.track_settings(p_track);
		// Native real_t components may exceed float32 precision in double builds.
		const bool wide = sizeof(real_t) > sizeof(float) && (settings.type == Animation::TYPE_POSITION_3D || settings.type == Animation::TYPE_ROTATION_3D || settings.type == Animation::TYPE_SCALE_3D || settings.type == Animation::TYPE_BEZIER);
		Vector<String> values;
		bool constant = !p_keys.is_empty();
		bool transitions = false;
		for (const auto &key : p_keys) {
			String encoded = key_value(key, wide, p_type == "variant");
			constant &= values.is_empty() || values[0] == encoded;
			values.push_back(encoded);
			transitions |= key.transition != (settings.type == Animation::TYPE_BEZIER ? 0 : 1);
		}
		String path = String(settings.path);
		if (!prefix.is_empty() && path.begins_with(prefix)) {
			path = quoted("." + path.substr(prefix.length()));
		} else if (!prefix.is_empty() && path.begins_with(".")) {
			path = "NodePath(" + quoted(path) + ")";
		} else {
			path = quoted(path);
		}
		Record record;
		record.opening = "\t\t(" + itos(settings.type) + ", " + path + ", " + itos(settings.interpolation) + ", " + itos(track_clocks[p_track]) + ", ";
		StringBuilder payload;
		String default_value;
		switch (settings.type) {
			case Animation::TYPE_POSITION_3D: default_value = "0, 0, 0"; break;
			case Animation::TYPE_ROTATION_3D: default_value = "0, 0, 0, 1"; break;
			case Animation::TYPE_SCALE_3D: default_value = "1, 1, 1"; break;
			case Animation::TYPE_BLEND_SHAPE: default_value = "0"; break;
			default: break;
		}
		if (constant && !default_value.is_empty() && values[0] == default_value) {
			payload += "d";
		} else {
			if (wide) {
				payload += "f64 ";
			}
			if (constant) {
				payload += (p_type.is_empty() ? String("c") : p_type) + "(" + values[0] + ")";
			} else {
				payload += p_type + "[";
				bool tuple = settings.type == Animation::TYPE_POSITION_3D || settings.type == Animation::TYPE_ROTATION_3D || settings.type == Animation::TYPE_SCALE_3D || settings.type == Animation::TYPE_BEZIER || settings.type == Animation::TYPE_AUDIO || p_type == "vec2" || p_type == "vec3" || p_type == "vec4" || p_type == "quat" || p_type == "color" || p_type == "transform3d";
				for (int i = 0; i < values.size(); i++) {
					if (i) {
						payload += ", ";
					}
					payload += tuple ? "(" + values[i] + ")" : values[i];
				}
				payload += "]";
			}
		}
		record.payload = payload.as_string();
		record.numeric = (settings.type >= Animation::TYPE_POSITION_3D && settings.type <= Animation::TYPE_BLEND_SHAPE) || settings.type == Animation::TYPE_BEZIER || settings.type == Animation::TYPE_AUDIO || p_type == "float" || p_type == "vec2" || p_type == "vec3" || p_type == "vec4" || p_type == "quat" || p_type == "color" || p_type == "transform3d";
		Vector<String> options;
		int flags = (!settings.enabled ? 1 : 0) | (settings.imported ? 2 : 0) | (!settings.loop_wrap ? 4 : 0);
		if (flags) {
			options.push_back("\"flags\": " + itos(flags));
		}
		if (settings.type == Animation::TYPE_VALUE && settings.update_mode != Animation::UPDATE_CONTINUOUS) {
			options.push_back("\"update\": " + itos(settings.update_mode));
		}
		if (settings.type == Animation::TYPE_AUDIO && !settings.audio_blend) {
			options.push_back("\"audio_blend\": false");
		}
		if (transitions) {
			String encoded;
			for (const auto &key : p_keys) {
				if (!encoded.is_empty()) {
					encoded += ", ";
				}
				encoded += w_animation_number(key.transition);
			}
			options.push_back(String("\"transitions\": PackedFloat") + (sizeof(real_t) == 8 ? "64" : "32") + "Array(" + encoded + ")");
		}
		handles(p_keys, options);
		if (!options.is_empty()) {
			record.options = ", {" + String(", ").join(options) + "}";
		}
		records.push_back(record);
	}

public:
	Writer(const WAnimation &p_animation, VariantWriter::EncodeResourceFunc p_encode, void *p_userdata) : animation(p_animation), encode(p_encode), userdata(p_userdata) {}

	Error write(String &r_text) {
		HashMap<String, int> prefixes;
		int best = 0;
		for (int i = 0; i < animation.get_track_count(); i++) {
			ERR_FAIL_COND_V_MSG(animation.track_is_compressed(i), ERR_UNAVAILABLE, "WTracks stores exact keys; decompress compressed tracks before saving as WAnimation.");
			String path = String(animation.track_get_path(i));
			int colon = path.rfind(":");
			if (colon >= 0) {
				String candidate = path.substr(0, colon + 1);
				int count = ++prefixes[candidate];
				if (count > best) {
					best = count;
					prefix = candidate;
				}
			}
			Vector<double> times;
			times.resize(animation.track_get_key_count(i));
			for (int k = 0; k < times.size(); k++) {
				times.write[k] = animation.track_get_key_time(i, k);
			}
			int clock = 0;
			while (clock < clocks.size()) {
				if (clocks[clock].size() == times.size() && (times.is_empty() || std::memcmp(clocks[clock].ptr(), times.ptr(), times.size() * sizeof(double)) == 0)) {
					break;
				}
				clock++;
			}
			if (clock == clocks.size()) {
				clocks.push_back(times);
			}
			track_clocks.push_back(clock);
		}
		Vector<String> clock_text;
		for (const auto &clock : clocks) {
			bool wide = false;
			for (double time : clock) {
				wide |= double(float(time)) != time;
			}
			StringBuilder encoded;
			encoded += wide ? "f64[" : "[";
			for (int i = 0; i < clock.size(); i++) {
				if (i) {
					encoded += ", ";
				}
				encoded += wide ? w_animation_number(clock[i]) : w_animation_number(float(clock[i]));
			}
			encoded += "]";
			clock_text.push_back(encoded.as_string());
			constants.count(clock_text[clock_text.size() - 1]);
		}
		for (int i = 0; i < animation.get_track_count(); i++) {
			switch (animation.track_get_type(i)) {
				case Animation::TYPE_VALUE: {
					const auto keys = animation.value_keys(i);
					track(i, keys, property_type(keys));
				} break;
				case Animation::TYPE_POSITION_3D:
				case Animation::TYPE_SCALE_3D: track(i, animation.vector_keys(i)); break;
				case Animation::TYPE_ROTATION_3D: track(i, animation.rotation_keys(i)); break;
				case Animation::TYPE_BLEND_SHAPE: track(i, animation.scalar_keys(i)); break;
				case Animation::TYPE_METHOD: track(i, animation.method_keys(i)); break;
				case Animation::TYPE_BEZIER: track(i, animation.bezier_keys(i)); break;
				case Animation::TYPE_AUDIO: track(i, animation.audio_keys(i)); break;
				case Animation::TYPE_ANIMATION: track(i, animation.animation_keys(i)); break;
			}
		}
		for (const Record &record : records) {
			if (record.numeric) {
				constants.count(record.payload);
			}
		}
#ifdef TOOLS_ENABLED
		constants.select(animation.get_codec_constants());
#else
		constants.select(Vector<WAnimation::CodecConstant>());
#endif
		text += "tracks = WTracks(\n\t2, " + quoted(prefix) + ",\n\t" + constants.declarations() + ",\n\t[\n";
		for (const String &clock : clock_text) {
			text += "\t\t" + constants.encode(clock) + ",\n";
		}
		text += "\t],\n\t[\n";
		for (const Record &record : records) {
			text += record.opening + (record.numeric ? constants.encode(record.payload) : record.payload) + record.options + "),\n";
		}
#ifdef TOOLS_ENABLED
		if (error == OK) {
			animation.set_codec_constants(constants.save_hints());
		}
#endif
		text += "\t]\n)\n";
		r_text = text.as_string();
		return error;
	}
};
} // namespace

Error WAnimationCodec::write(const Resource *p_resource, String &r_text, VariantWriter::EncodeResourceFunc p_encode, void *p_userdata) const {
	const WAnimation *animation = Object::cast_to<WAnimation>(p_resource);
	ERR_FAIL_NULL_V(animation, ERR_INVALID_PARAMETER);
	return Writer(*animation, p_encode, p_userdata).write(r_text);
}
