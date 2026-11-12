// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/templates/local_vector.h"

class WAnimationPose : public RefCounted {
	GDCLASS(WAnimationPose, RefCounted);
	friend class WAnimationTracks;

	struct Bone {
		Quaternion rotation;
		Vector3 translation;
		Vector3 scale = Vector3(1, 1, 1);
	};
	LocalVector<Bone> bones;

protected:
	static void _bind_methods();

public:
	int get_bone_count() const { return bones.size(); }
	Quaternion get_rotation(int p_index) const;
	Vector3 get_translation(int p_index) const;
	Vector3 get_scale(int p_index) const;
	Transform3D get_transform(int p_index) const;
};

// Compressed tracks are shared resource data. Decoder contexts live on the
// sampling thread's stack; each playback instance owns its reusable pose.
class WAnimationTracks : public Resource {
	GDCLASS(WAnimationTracks, Resource);

	PackedByteArray data;
	int data_offset = 0;
	int data_size = 0;
	bool valid = false;
	const uint8_t *track_data() const { return data.ptr() + data_offset; }

protected:
	static void _bind_methods();

public:
	void set_data(const PackedByteArray &p_data);
	PackedByteArray get_data() const;
	int64_t get_storage_size() const { return data.size(); }
	bool is_valid() const { return valid; }
	bool is_transform() const;
	bool requires_reference_pose() const;
	int get_track_count() const;
	int get_sample_count() const;
	double get_sample_rate() const;
	double get_duration() const;
	Error sample_pose(double p_time, const PackedInt32Array &p_tracks, const Ref<WAnimationPose> &p_pose, const PackedFloat32Array &p_reference_pose, const PackedInt32Array &p_track_to_bone) const;
	double sample_curve(int p_track, double p_time) const;
};
