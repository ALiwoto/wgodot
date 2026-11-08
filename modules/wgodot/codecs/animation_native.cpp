// wgodot-changes::file
#include "animation_codec.h"
#include "animation_payload.h"

namespace {
using NativeWriter = WResourceCodec::NativeWriter;

void native_value(NativeWriter &p_writer, const String &p_key, const WAnimation::AudioValue &p_value) {
	const String stream = p_writer.value(p_value.stream);
	p_writer.line(p_key + ".value.stream = " + stream + ";");
	p_writer.line(p_key + ".value.start_offset = " + p_writer.value(p_value.start_offset) + ";");
	p_writer.line(p_key + ".value.end_offset = " + p_writer.value(p_value.end_offset) + ";");
}

template <typename T>
void native_key(NativeWriter &p_writer, const String &p_key, const T &p_value) {
	native_value(p_writer, p_key, p_value.value);
}

void native_key(NativeWriter &p_writer, const String &p_key, const WAnimation::MethodKey &p_value) {
	const String method = p_writer.value(p_value.method);
	p_writer.line(p_key + ".method = " + method + ";");
	p_writer.line(p_key + ".params.resize(" + itos(p_value.params.size()) + ");");
	for (int i = 0; i < p_value.params.size(); i++) {
		const String argument = p_writer.value(p_value.params[i]);
		p_writer.line(p_key + ".params.write[" + itos(i) + "] = " + argument + ";");
	}
}

template <typename T>
void native_keys(NativeWriter &p_writer, const String &p_target, const String &p_index, const String &p_key_type, const LocalVector<T> &p_keys) {
	if (p_keys.is_empty()) {
		return;
	}
	const String keys = p_writer.symbol();
	p_writer.line("LocalVector<WAnimation::" + p_key_type + "> " + keys + ";");
	p_writer.line(keys + ".resize(" + itos(p_keys.size()) + ");");
	for (uint32_t i = 0; i < p_keys.size(); i++) {
		const String key = keys + "[" + itos(i) + "]";
		p_writer.line(key + ".time = " + p_writer.value(p_keys[i].time) + ";");
		if (p_keys[i].transition != 1) {
			p_writer.line(key + ".transition = " + p_writer.value(p_keys[i].transition) + ";");
		}
		native_key(p_writer, key, p_keys[i]);
	}
	p_writer.line("ERR_FAIL_COND_V(" + p_target + "->set_keyframes(" + p_index + ", std::move(" + keys + ")) != OK, Ref<Resource>());");
}

template <typename T>
void numeric_keys(NativeWriter &p_writer, const String &p_target, const String &p_index, const String &p_key_type, const LocalVector<T> &p_keys) {
	const String keys = p_writer.symbol();
	const String type = "LocalVector<WAnimation::" + p_key_type + ">";
	const String data = p_writer.payload(type, WGodotNativePayload::ANIMATION_KEYS, [&](FileAccess &file) {
		WGodotNativePayload::Value<LocalVector<T>>::write(file, p_keys);
	});
	p_writer.line(type + " " + keys + " = " + data + ";");
	p_writer.line("ERR_FAIL_COND_V(" + p_target + "->set_keyframes(" + p_index + ", std::move(" + keys + ")) != OK, Ref<Resource>());");
}

Error property_keys(NativeWriter &p_writer, const String &p_target, const String &p_index, const WAnimationValues &p_values) {
	if (p_values.is_empty()) {
		return OK;
	}
	ERR_FAIL_COND_V_MSG(p_values.kind() == WAnimationValueKind::Generic, ERR_INVALID_DATA, "Native animation tracks require a concrete value type; heterogeneous nonnumeric keys cannot be compiled as a typed track.");
	String cpp_type;
	switch (p_values.kind()) {
#define W_VALUE_CPP(m_name, m_type, m_variant) \
	case WAnimationValueKind::m_name: \
		cpp_type = #m_type; \
		break;
		W_ANIMATION_VALUE_TYPES(W_VALUE_CPP)
#undef W_VALUE_CPP
		case WAnimationValueKind::Number:
			cpp_type = "WAnimationNumber";
			break;
		default:
			return ERR_INVALID_DATA;
	}
	return p_values.visit([&](const auto &values) -> Error {
		using T = std::decay_t<decltype(values[0])>;
		if constexpr (!std::is_same_v<T, Variant>) {
			const String keys = p_writer.symbol();
			const String key_type = "WAnimation::TypedValueKey<" + cpp_type + ">";
			if constexpr (std::is_same_v<T, Ref<Resource>> || std::is_same_v<T, RID>) {
				// Resource references belong to the generated dependency graph.
				p_writer.line("LocalVector<" + key_type + "> " + keys + ";");
				p_writer.line(keys + ".resize(" + itos(values.size()) + ");");
				for (uint32_t i = 0; i < values.size(); i++) {
					const String key = keys + "[" + itos(i) + "]";
					p_writer.line(key + ".time = " + p_writer.value(p_values.keys[i].time) + ";");
					p_writer.line(key + ".transition = " + p_writer.value(p_values.keys[i].transition) + ";");
					p_writer.line(key + ".value = " + p_writer.value(values[i]) + ";");
				}
			} else {
				const String type = "LocalVector<" + key_type + ">";
				const String data = p_writer.payload(type, WGodotNativePayload::ANIMATION_KEYS, [&](FileAccess &file) {
					file.store_64(values.size());
					for (uint32_t i = 0; i < values.size(); i++) {
						WGodotNativePayload::Value<real_t>::write(file, p_values.keys[i].transition);
						WGodotNativePayload::Value<double>::write(file, p_values.keys[i].time);
						WGodotNativePayload::Value<T>::write(file, values[i]);
					}
				});
				p_writer.line(type + " " + keys + " = " + data + ";");
			}
			p_writer.line("ERR_FAIL_COND_V(" + p_target + "->set_value_keyframes<" + cpp_type + ">(" + p_index + ", " + keys + ".ptr(), " + keys + ".size()) != OK, Ref<Resource>());");
			if (!p_values.get_timing_curves().is_empty()) {
				const String curves = p_writer.value(p_values.get_timing_curves());
				const String subdivisions = p_writer.value(p_values.get_timing_curve_subdivisions());
				p_writer.line(p_target + "->value_track_set_timing_curves(" + p_index + ", " + curves + ", " + subdivisions + ");");
			}
			return OK;
		}
		return ERR_INVALID_DATA;
	});
}
} // namespace

Error WAnimationCodec::write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const {
	const WAnimation *animation = Object::cast_to<WAnimation>(p_resource);
	ERR_FAIL_NULL_V(animation, ERR_INVALID_PARAMETER);
	p_writer.include("modules/wgodot/codecs/animation_payload.h");
	for (int i = 0; i < animation->get_track_count(); i++) {
		ERR_FAIL_COND_V(animation->track_is_compressed(i), ERR_UNAVAILABLE);
		const auto settings = animation->track_settings(i);
		if (settings.type == Animation::TYPE_VALUE || settings.type == Animation::TYPE_BEZIER) {
			p_writer.animation_property(settings.path, settings.type == Animation::TYPE_BEZIER ? WAnimationValueKind::Float : animation->value_track_storage(i).kind());
		}
		const WAnimation::TrackSettings defaults;
		const String config = p_writer.symbol();
		const String index = p_writer.symbol();
		// Each temporary key buffer is released immediately after bulk transfer.
		p_writer.line("{");
		p_writer.line("WAnimation::TrackSettings " + config + ";");
		if (settings.type != defaults.type) {
			p_writer.line(config + ".type = Animation::TrackType(" + itos(settings.type) + ");");
		}
		const String path = p_writer.value(settings.path);
		p_writer.line(config + ".path = " + path + ";");
		if (settings.interpolation != defaults.interpolation) {
			p_writer.line(config + ".interpolation = Animation::InterpolationType(" + itos(settings.interpolation) + ");");
		}
		if (settings.enabled != defaults.enabled) {
			p_writer.line(config + ".enabled = " + (settings.enabled ? "true" : "false") + ";");
		}
		if (settings.imported != defaults.imported) {
			p_writer.line(config + ".imported = " + (settings.imported ? "true" : "false") + ";");
		}
		if (settings.loop_wrap != defaults.loop_wrap) {
			p_writer.line(config + ".loop_wrap = " + (settings.loop_wrap ? "true" : "false") + ";");
		}
		if (settings.type == Animation::TYPE_VALUE && settings.update_mode != defaults.update_mode) {
			p_writer.line(config + ".update_mode = Animation::UpdateMode(" + itos(settings.update_mode) + ");");
		} else if (settings.type == Animation::TYPE_AUDIO && settings.audio_blend != defaults.audio_blend) {
			p_writer.line(config + ".audio_blend = " + (settings.audio_blend ? "true" : "false") + ";");
		}
		p_writer.line("const int " + index + " = " + p_target + "->append_track(" + config + ");");
		switch (settings.type) {
			case Animation::TYPE_VALUE: {
				RETURN_IF_ERROR(property_keys(p_writer, p_target, index, animation->value_track_storage(i)));
			} break;
			case Animation::TYPE_POSITION_3D:
			case Animation::TYPE_SCALE_3D:
				numeric_keys(p_writer, p_target, index, "VectorKey", animation->vector_keys(i));
				break;
			case Animation::TYPE_ROTATION_3D:
				numeric_keys(p_writer, p_target, index, "RotationKey", animation->rotation_keys(i));
				break;
			case Animation::TYPE_BLEND_SHAPE:
				numeric_keys(p_writer, p_target, index, "ScalarKey", animation->scalar_keys(i));
				break;
			case Animation::TYPE_METHOD:
				native_keys(p_writer, p_target, index, "MethodKey", animation->method_keys(i));
				break;
			case Animation::TYPE_BEZIER:
				numeric_keys(p_writer, p_target, index, "BezierKey", animation->bezier_keys(i));
				break;
			case Animation::TYPE_AUDIO:
				native_keys(p_writer, p_target, index, "AudioKey", animation->audio_keys(i));
				break;
			case Animation::TYPE_ANIMATION:
				numeric_keys(p_writer, p_target, index, "AnimationKey", animation->animation_keys(i));
				break;
		}
		p_writer.line("}");
	}
	return OK;
}
