// wgodot-changes::file
#include "animation_tracks.h"

#include "core/object/class_db.h"

#include <acl/core/compressed_tracks.h>
#include <acl/decompression/decompress.h>
#include <acl/core/track_writer.h>

namespace {

const acl::compressed_tracks &tracks(const uint8_t *p_data) {
	return *reinterpret_cast<const acl::compressed_tracks *>(p_data);
}

struct PoseWriter : acl::track_writer {
	const float *reference = nullptr;
	Quaternion rotation;
	Vector3 translation;
	Vector3 scale = Vector3(1, 1, 1);
	static constexpr acl::default_sub_track_mode get_default_rotation_mode() { return acl::default_sub_track_mode::variable; }
	static constexpr acl::default_sub_track_mode get_default_translation_mode() { return acl::default_sub_track_mode::variable; }
	static constexpr acl::default_sub_track_mode get_default_scale_mode() { return acl::default_sub_track_mode::variable; }
	rtm::quatf RTM_SIMD_CALL get_variable_default_rotation(uint32_t) const {
		return rtm::quat_set(reference[0], reference[1], reference[2], reference[3]);
	}
	rtm::vector4f RTM_SIMD_CALL get_variable_default_translation(uint32_t) const {
		return rtm::vector_set(reference[4], reference[5], reference[6]);
	}
	rtm::vector4f RTM_SIMD_CALL get_variable_default_scale(uint32_t) const {
		return rtm::vector_set(reference[7], reference[8], reference[9]);
	}

	void RTM_SIMD_CALL write_rotation(uint32_t, rtm::quatf_arg0 p_value) {
		rotation = Quaternion(rtm::quat_get_x(p_value), rtm::quat_get_y(p_value), rtm::quat_get_z(p_value), rtm::quat_get_w(p_value));
	}
	void RTM_SIMD_CALL write_translation(uint32_t, rtm::vector4f_arg0 p_value) {
		translation = Vector3(rtm::vector_get_x(p_value), rtm::vector_get_y(p_value), rtm::vector_get_z(p_value));
	}
	void RTM_SIMD_CALL write_scale(uint32_t, rtm::vector4f_arg0 p_value) {
		scale = Vector3(rtm::vector_get_x(p_value), rtm::vector_get_y(p_value), rtm::vector_get_z(p_value));
	}
};

struct CurveWriter : acl::track_writer {
	float value = 0;
	void RTM_SIMD_CALL write_float1(uint32_t, rtm::scalarf_arg0 p_value) {
		value = rtm::scalar_cast(p_value);
	}
};

} // namespace

void WAnimationPose::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bone_count"), &WAnimationPose::get_bone_count);
	ClassDB::bind_method(D_METHOD("get_rotation", "index"), &WAnimationPose::get_rotation);
	ClassDB::bind_method(D_METHOD("get_translation", "index"), &WAnimationPose::get_translation);
	ClassDB::bind_method(D_METHOD("get_scale", "index"), &WAnimationPose::get_scale);
	ClassDB::bind_method(D_METHOD("get_transform", "index"), &WAnimationPose::get_transform);
}

Quaternion WAnimationPose::get_rotation(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(bones.size()), Quaternion());
	return bones[p_index].rotation;
}

Vector3 WAnimationPose::get_translation(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(bones.size()), Vector3());
	return bones[p_index].translation;
}

Vector3 WAnimationPose::get_scale(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(bones.size()), Vector3(1, 1, 1));
	return bones[p_index].scale;
}

Transform3D WAnimationPose::get_transform(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, int(bones.size()), Transform3D());
	const Bone &bone = bones[p_index];
	return Transform3D(Basis(bone.rotation).scaled_local(bone.scale), bone.translation);
}

void WAnimationTracks::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &WAnimationTracks::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &WAnimationTracks::get_data);
	ClassDB::bind_method(D_METHOD("get_storage_size"), &WAnimationTracks::get_storage_size);
	ClassDB::bind_method(D_METHOD("is_valid"), &WAnimationTracks::is_valid);
	ClassDB::bind_method(D_METHOD("is_transform"), &WAnimationTracks::is_transform);
	ClassDB::bind_method(D_METHOD("requires_reference_pose"), &WAnimationTracks::requires_reference_pose);
	ClassDB::bind_method(D_METHOD("get_track_count"), &WAnimationTracks::get_track_count);
	ClassDB::bind_method(D_METHOD("get_sample_count"), &WAnimationTracks::get_sample_count);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &WAnimationTracks::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_duration"), &WAnimationTracks::get_duration);
	ClassDB::bind_method(D_METHOD("sample_pose", "time", "tracks", "pose", "reference_pose", "track_to_bone"), &WAnimationTracks::sample_pose, DEFVAL(PackedFloat32Array()), DEFVAL(PackedInt32Array()));
	ClassDB::bind_method(D_METHOD("sample_curve", "track", "time"), &WAnimationTracks::sample_curve);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data"), "set_data", "get_data");
}

void WAnimationTracks::set_data(const PackedByteArray &p_data) {
	if (p_data.is_empty()) {
		valid = false;
		data = p_data;
		data_offset = 0;
		data_size = 0;
		emit_changed();
		return;
	}
	ERR_FAIL_COND_MSG(p_data.size() < 32 || p_data.size() > INT32_MAX - 15, "Invalid compressed animation size.");
	PackedByteArray storage = p_data;
	int offset = 0;
	if (reinterpret_cast<uintptr_t>(p_data.ptr()) & 15) {
		storage = PackedByteArray();
		ERR_FAIL_COND(storage.resize(p_data.size() + 15) != OK);
		uint8_t *buffer = storage.ptrw();
		offset = (16 - (reinterpret_cast<uintptr_t>(buffer) & 15)) & 15;
		memcpy(buffer + offset, p_data.ptr(), p_data.size());
	}
	const acl::compressed_tracks &clip = tracks(storage.ptr() + offset);
	ERR_FAIL_COND_MSG(clip.get_size() != p_data.size(), "Compressed animation size differs from its payload.");
	const acl::error_result error = clip.is_valid(true);
	ERR_FAIL_COND_MSG(error.any(), String("Invalid compressed animation: ") + error.c_str());
	ERR_FAIL_COND_MSG(clip.get_track_type() != acl::track_type8::qvvf && clip.get_track_type() != acl::track_type8::float1f, "Expected transform or scalar animation tracks.");
	ERR_FAIL_COND_MSG(clip.get_num_tracks() == 0 || clip.get_num_tracks() > INT32_MAX || clip.get_num_samples_per_track() == 0 || !Math::is_finite(clip.get_sample_rate()) || clip.get_sample_rate() <= 0, "Invalid compressed animation dimensions.");
	if (clip.get_track_type() == acl::track_type8::qvvf) {
		acl::decompression_context<acl::debug_transform_decompression_settings> context;
		ERR_FAIL_COND_MSG(!context.initialize(clip), "Unsupported compressed transform animation.");
	} else {
		acl::decompression_context<acl::debug_scalar_decompression_settings> context;
		ERR_FAIL_COND_MSG(!context.initialize(clip), "Unsupported compressed scalar animation.");
	}
	data = storage;
	data_offset = offset;
	data_size = p_data.size();
	valid = true;
	emit_changed();
}

PackedByteArray WAnimationTracks::get_data() const {
	return data_offset == 0 && data_size == data.size() ? data : data.slice(data_offset, data_offset + data_size);
}

bool WAnimationTracks::is_transform() const {
	return valid && tracks(track_data()).get_track_type() == acl::track_type8::qvvf;
}

bool WAnimationTracks::requires_reference_pose() const {
	return is_transform() && !tracks(track_data()).has_trivial_default_values();
}

int WAnimationTracks::get_track_count() const {
	return valid ? tracks(track_data()).get_num_tracks() : 0;
}

int WAnimationTracks::get_sample_count() const {
	return valid ? tracks(track_data()).get_num_samples_per_track() : 0;
}

double WAnimationTracks::get_sample_rate() const {
	return valid ? tracks(track_data()).get_sample_rate() : 0;
}

double WAnimationTracks::get_duration() const {
	return valid ? tracks(track_data()).get_duration(acl::sample_looping_policy::clamp) : 0;
}

Error WAnimationTracks::sample_pose(double p_time, const PackedInt32Array &p_tracks, const Ref<WAnimationPose> &p_pose, const PackedFloat32Array &p_reference_pose, const PackedInt32Array &p_track_to_bone) const {
	ERR_FAIL_COND_V(!is_transform() || p_pose.is_null() || !Math::is_finite(p_time), ERR_INVALID_PARAMETER);
	const acl::compressed_tracks &clip = tracks(track_data());
	const bool reference_needed = !clip.has_trivial_default_values();
	ERR_FAIL_COND_V(reference_needed && (p_track_to_bone.size() != clip.get_num_tracks() || p_reference_pose.size() % 10 != 0), ERR_INVALID_PARAMETER);
	for (int32_t track : p_tracks) {
		ERR_FAIL_INDEX_V(track, int(clip.get_num_tracks()), ERR_INVALID_PARAMETER);
		if (reference_needed) {
			ERR_FAIL_INDEX_V(p_track_to_bone[track], p_reference_pose.size() / 10, ERR_INVALID_PARAMETER);
		}
	}
	acl::decompression_context<acl::debug_transform_decompression_settings> context;
	ERR_FAIL_COND_V(!context.initialize(clip), ERR_INVALID_DATA);
	context.seek(p_time, acl::sample_rounding_policy::none);
	if (p_pose->bones.size() != uint32_t(p_tracks.size())) {
		p_pose->bones.resize(p_tracks.size());
	}
	const float default_scale = clip.get_default_scale();
	const float default_pose[10] = { 0, 0, 0, 1, 0, 0, 0, default_scale, default_scale, default_scale };
	for (int i = 0; i < p_tracks.size(); i++) {
		PoseWriter writer;
		writer.reference = reference_needed ? p_reference_pose.ptr() + p_track_to_bone[p_tracks[i]] * 10 : default_pose;
		context.decompress_track(p_tracks[i], writer);
		WAnimationPose::Bone &bone = p_pose->bones[i];
		bone.rotation = writer.rotation;
		bone.translation = writer.translation;
		bone.scale = writer.scale;
	}
	return OK;
}

double WAnimationTracks::sample_curve(int p_track, double p_time) const {
	ERR_FAIL_COND_V(!valid || is_transform() || !Math::is_finite(p_time), 0);
	const acl::compressed_tracks &clip = tracks(track_data());
	ERR_FAIL_INDEX_V(p_track, int(clip.get_num_tracks()), 0);
	acl::decompression_context<acl::debug_scalar_decompression_settings> context;
	ERR_FAIL_COND_V(!context.initialize(clip), 0);
	context.seek(p_time, acl::sample_rounding_policy::none);
	CurveWriter writer;
	context.decompress_track(p_track, writer);
	return writer.value;
}
