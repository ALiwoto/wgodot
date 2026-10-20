// wgodot-changes::file
#include "animation_mixer.h"

void AnimationMixer::set_animation_library(const StringName &p_name, const Ref<AnimationLibrary> &p_library) {
	if (has_animation_library(p_name)) {
		remove_animation_library(p_name);
	}
	add_animation_library(p_name, p_library);
	emit_signal(SNAME("animation_libraries_updated"));
}

void AnimationMixer::clear_animation_libraries() {
	while (!animation_libraries.is_empty()) {
		remove_animation_library(animation_libraries[0].name);
	}
	emit_signal(SNAME("animation_libraries_updated"));
}
