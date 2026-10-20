// wgodot-changes::file
#include "animation_mixer.h"
#ifndef _3D_DISABLED
#include "scene/3d/skeleton_3d.h"
#endif

WAnimationValuePostProcess AnimationMixer::value_post_process(const Ref<Animation> &p_animation, int p_track, ObjectID p_object) {
	WAnimationValuePostProcess result;
	if (is_GDVIRTUAL_CALL_post_process_key_value) {
		result.context = this;
		result.animation = p_animation.ptr();
		result.track = p_track;
		result.object = p_object;
		result.invoke = [](void *p_context, const Animation *p_source, int p_index, const Variant &p_value, ObjectID p_target, int p_sub_index) -> Variant {
			auto *mixer = static_cast<AnimationMixer *>(p_context);
			return mixer->post_process_key_value(Ref<Animation>(const_cast<Animation *>(p_source)), p_index, p_value, p_target, p_sub_index);
		};
	}
	return result;
}

Vector3 AnimationMixer::post_process_key_value(const Ref<Animation> &p_anim, int p_track, Vector3 p_value, ObjectID p_object_id, int p_object_sub_idx) {
	if (is_GDVIRTUAL_CALL_post_process_key_value) {
		return post_process_key_value(p_anim, p_track, Variant(p_value), p_object_id, p_object_sub_idx);
	}
#ifndef _3D_DISABLED
	if (p_anim->track_get_type(p_track) == Animation::TYPE_POSITION_3D && p_object_sub_idx >= 0) {
		if (Skeleton3D *skeleton = ObjectDB::get_instance<Skeleton3D>(p_object_id)) {
			p_value *= skeleton->get_motion_scale();
		}
	}
#endif
	return p_value;
}

Quaternion AnimationMixer::post_process_key_value(const Ref<Animation> &p_anim, int p_track, Quaternion p_value, ObjectID p_object_id, int p_object_sub_idx) {
	if (is_GDVIRTUAL_CALL_post_process_key_value) {
		return post_process_key_value(p_anim, p_track, Variant(p_value), p_object_id, p_object_sub_idx);
	}
	return p_value;
}

float AnimationMixer::post_process_key_value(const Ref<Animation> &p_anim, int p_track, float p_value, ObjectID p_object_id, int p_object_sub_idx) {
	if (is_GDVIRTUAL_CALL_post_process_key_value) {
		return post_process_key_value(p_anim, p_track, Variant(p_value), p_object_id, p_object_sub_idx);
	}
	return p_value;
}
