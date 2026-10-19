// wgodot-changes::file
#include "animation_codec.h"
#include "native_literals.h"

namespace {
using NativeWriter = WResourceCodec::NativeWriter;

void native_value(NativeWriter &p_writer, const String &p_key, const Variant &p_value) {
	// Only actual property keys use Variant, matching Animation::ValueTrack.
	const String value = p_writer.value(p_value);
	p_writer.line(p_key + ".value = " + value + ";");
}

void native_value(NativeWriter &p_writer, const String &p_key, const StringName &p_value) {
	const String value = p_writer.value(p_value);
	p_writer.line(p_key + ".value = " + value + ";");
}

void native_value(NativeWriter &p_writer, const String &p_key, const WAnimation::BezierValue &p_value) {
	p_writer.line(p_key + ".value.value = " + WGodotCppLiterals::number(p_value.value) + ";");
	p_writer.line(p_key + ".value.in_handle = Vector2(" + WGodotCppLiterals::number(p_value.in_handle.x) + ", " + WGodotCppLiterals::number(p_value.in_handle.y) + ");");
	p_writer.line(p_key + ".value.out_handle = Vector2(" + WGodotCppLiterals::number(p_value.out_handle.x) + ", " + WGodotCppLiterals::number(p_value.out_handle.y) + ");");
#ifdef TOOLS_ENABLED
	p_writer.line("#ifdef TOOLS_ENABLED");
	p_writer.line(p_key + ".value.handle_mode = Animation::HandleMode(" + itos(p_value.handle_mode) + ");");
	p_writer.line("#endif");
#endif
}

void native_value(NativeWriter &p_writer, const String &p_key, const WAnimation::AudioValue &p_value) {
	const String stream = p_writer.value(p_value.stream);
	p_writer.line(p_key + ".value.stream = " + stream + ";");
	p_writer.line(p_key + ".value.start_offset = " + WGodotCppLiterals::number(p_value.start_offset) + ";");
	p_writer.line(p_key + ".value.end_offset = " + WGodotCppLiterals::number(p_value.end_offset) + ";");
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
		p_writer.line(key + ".time = " + WGodotCppLiterals::number(p_keys[i].time) + ";");
		if (p_keys[i].transition != 1) {
			p_writer.line(key + ".transition = " + WGodotCppLiterals::number(p_keys[i].transition) + ";");
		}
		native_key(p_writer, key, p_keys[i]);
	}
	p_writer.line("ERR_FAIL_COND_V(" + p_target + "->set_keyframes(" + p_index + ", std::move(" + keys + ")) != OK, Ref<Resource>());");
}

template <typename T>
void numeric_keys(NativeWriter &p_writer, const String &p_target, const String &p_index, const String &p_key_type, const LocalVector<T> &p_keys) {
	if (p_keys.is_empty()) {
		return;
	}
	const String keys = p_writer.symbol();
	p_writer.line("static constexpr WAnimation::" + p_key_type + " " + keys + "[] = {");
	for (const T &key : p_keys) {
		String value;
		if constexpr (std::is_same_v<T, WAnimation::RotationKey>) {
			value = WGodotCppLiterals::components(key.value, 4);
		} else {
			value = WGodotCppLiterals::packed_element(key.value);
		}
		// Animation::TKey stores its Key base (transition, time), then value.
		p_writer.line("\t{ { " + WGodotCppLiterals::number(key.transition) + ", " + WGodotCppLiterals::number(key.time) + " }, " + value + " },");
	}
	p_writer.line("};");
	p_writer.line("ERR_FAIL_COND_V(" + p_target + "->set_keyframes(" + p_index + ", " + keys + ", " + itos(p_keys.size()) + ") != OK, Ref<Resource>());");
}
} // namespace

Error WAnimationCodec::write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const {
	const WAnimation *animation = Object::cast_to<WAnimation>(p_resource);
	ERR_FAIL_NULL_V(animation, ERR_INVALID_PARAMETER);
	p_writer.include("modules/wgodot/codecs/w_animation.h");
	for (int i = 0; i < animation->get_track_count(); i++) {
		ERR_FAIL_COND_V(animation->track_is_compressed(i), ERR_UNAVAILABLE);
		const auto settings = animation->track_settings(i);
		if (settings.type == Animation::TYPE_VALUE || settings.type == Animation::TYPE_BEZIER) {
			p_writer.animation_property(settings.path);
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
			case Animation::TYPE_VALUE: native_keys(p_writer, p_target, index, "ValueKey", animation->value_keys(i)); break;
			case Animation::TYPE_POSITION_3D:
			case Animation::TYPE_SCALE_3D: numeric_keys(p_writer, p_target, index, "VectorKey", animation->vector_keys(i)); break;
			case Animation::TYPE_ROTATION_3D: numeric_keys(p_writer, p_target, index, "RotationKey", animation->rotation_keys(i)); break;
			case Animation::TYPE_BLEND_SHAPE: numeric_keys(p_writer, p_target, index, "ScalarKey", animation->scalar_keys(i)); break;
			case Animation::TYPE_METHOD: native_keys(p_writer, p_target, index, "MethodKey", animation->method_keys(i)); break;
			case Animation::TYPE_BEZIER: native_keys(p_writer, p_target, index, "BezierKey", animation->bezier_keys(i)); break;
			case Animation::TYPE_AUDIO: native_keys(p_writer, p_target, index, "AudioKey", animation->audio_keys(i)); break;
			case Animation::TYPE_ANIMATION: native_keys(p_writer, p_target, index, "AnimationKey", animation->animation_keys(i)); break;
		}
		p_writer.line("}");
	}
	return OK;
}
