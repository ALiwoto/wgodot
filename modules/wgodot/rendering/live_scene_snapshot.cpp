// wgodot-changes::file
#include "live_scene_probe.h"
#ifdef TOOLS_ENABLED
#include "core/io/stream_peer.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/os/thread.h"
#include "core/templates/hash_set.h"
#include "render_capture.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_3d.h"
#include "modules/particle_effects/particle_instance_pool_3d.h"
#endif

namespace {
// Wire version 2. Records carry typed numbers/strings/raw uniform words, never
// Variant dictionaries, reflection property lists, texture pixels or vertices.
// See tools/porting/live/wire.py in the client project's research tooling.
enum Kind { SPATIAL = 1, SKELETON, MESH, MATERIAL, UNIFORM, PARTICLE, CAMERA, LIGHT, DECAL, AUDIO, ANIMATION, BODY, GEOMETRY };

struct Writer {
	Ref<StreamPeerBuffer> output;
	HashMap<ObjectID, String> materials;
	Writer() {
		output.instantiate();
		output->put_u32(0x324c5357); // WSL2
	}
	void record(Kind p_kind, const String &p_path, const Vector<String> &p_strings, const Vector<double> &p_values, const Vector<uint8_t> &p_raw = {}) {
		output->put_u16(p_kind);
		output->put_utf8_string(p_path);
		output->put_u32(p_strings.size());
		for (const String &value : p_strings) {
			output->put_utf8_string(value);
		}
		output->put_u32(p_values.size());
		for (double value : p_values) {
			output->put_double(value);
		}
		output->put_u32(p_raw.size());
		if (!p_raw.is_empty()) {
			output->put_data(p_raw.ptr(), p_raw.size());
		}
	}
	static void vector(Vector<double> &r_values, const Vector3 &p_value) {
		r_values.append_array({ p_value.x, p_value.y, p_value.z });
	}
	static void color(Vector<double> &r_values, const Color &p_value) {
		r_values.append_array({ p_value.r, p_value.g, p_value.b, p_value.a });
	}
	static void transform(Vector<double> &r_values, const Transform3D &p_value) {
		for (int column = 0; column < 3; column++) {
			vector(r_values, p_value.basis.get_column(column));
		}
		vector(r_values, p_value.origin);
	}
	void material(const Ref<Material> &p_material, const String &p_path) {
		if (p_material.is_null()) {
			record(MATERIAL, p_path, {}, {});
			return;
		}
		const String *existing = materials.getptr(p_material->get_instance_id());
		if (existing) {
			record(MATERIAL, p_path, { "reference", *existing }, {});
			return;
		}
		materials.insert(p_material->get_instance_id(), p_path);
		Ref<ShaderMaterial> shader_material = p_material;
		Ref<Shader> shader = shader_material.is_valid() ? shader_material->get_shader() : Ref<Shader>();
		record(MATERIAL, p_path, { p_material->get_class(), p_material->get_path(), shader.is_valid() ? shader->get_path() : String() }, { double(p_material->get_render_priority()) });
		if (shader_material.is_valid() && shader.is_valid()) {
			const WShaderParameters &parameters = shader_material->get_shader_parameters();
			if (parameters.layout.is_valid()) {
				for (int slot = 0; slot < parameters.layout->entries.size(); slot++) {
					const WShaderParameterLayout::Entry &entry = parameters.layout->entries[slot];
					if (!entry.valid()) {
						continue;
					}
					Vector<String> strings = { entry.name };
					Vector<uint8_t> raw;
					if (entry.texture >= 0) {
						for (int i = 0; i < entry.count; i++) {
							Ref<Texture> texture = shader_material->get_shader_texture_at(slot, i);
							strings.push_back(texture.is_valid() ? texture->get_path() : String());
						}
					} else {
						WShaderParameterUpdate update = parameters.update(slot);
						const uint32_t *words = update.array_words.is_empty() ? update.words : update.array_words.ptr();
						int count = entry.stride * entry.count;
						raw.resize(count * sizeof(uint32_t));
						for (int i = 0; i < count; i++) {
							encode_uint32(words[i], raw.ptrw() + i * sizeof(uint32_t));
						}
					}
					record(UNIFORM, p_path, strings, { double(entry.type), double(entry.hint), double(entry.count), double(entry.stride), double(entry.texture) }, raw);
				}
			}
		}
		// Next-pass materials are independent draw inputs, not replacements.
		if (p_material->get_next_pass().is_valid()) {
			material(p_material->get_next_pass(), p_path + "/next");
		}
	}
};
}

void WLiveSceneProbe::skeleton_updated(ObjectID p_id) {
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(ObjectDB::get_instance(p_id));
	if (!skeleton) {
		skeleton_poses.erase(p_id);
		return;
	}
	SkeletonPose &pose = skeleton_poses[p_id];
	int count = skeleton->get_bone_count();
	if (pose.version != skeleton->get_version() || pose.names.size() != count) {
		pose.names.resize(count);
		for (int i = 0; i < count; i++) {
			pose.names.write[i] = skeleton->get_bone_name(i);
		}
		pose.version = skeleton->get_version();
	}
	pose.transforms.resize(count);
	for (int i = 0; i < count; i++) {
		pose.transforms.write[i] = skeleton->get_bone_global_pose(i);
	}
}

const WLiveSceneProbe::SkeletonPose &WLiveSceneProbe::observe_skeleton(Skeleton3D *p_skeleton) {
	ObjectID id = p_skeleton->get_instance_id();
	if (!skeleton_poses.has(id)) {
		p_skeleton->connect(SNAME("skeleton_updated"), callable_mp(this, &WLiveSceneProbe::skeleton_updated).bind(id));
		skeleton_updated(id);
	}
	return skeleton_poses[id];
}

void WLiveSceneProbe::release_skeletons() {
	for (const KeyValue<ObjectID, SkeletonPose> &entry : skeleton_poses) {
		Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(ObjectDB::get_instance(entry.key));
		if (skeleton) {
			skeleton->disconnect(SNAME("skeleton_updated"), callable_mp(this, &WLiveSceneProbe::skeleton_updated).bind(entry.key));
		}
	}
	skeleton_poses.clear();
}

void WLiveSceneProbe::node_added(Node *p_node) {
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(snapshot_root));
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
	if (root && skeleton && root->is_ancestor_of(skeleton)) {
		observe_skeleton(skeleton);
	}
}

PackedByteArray WLiveSceneProbe::snapshot(Node *p_root, Skeleton3D *p_body) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), PackedByteArray());
	ERR_FAIL_NULL_V(p_root, PackedByteArray());
	snapshot_root = p_root->get_instance_id();
	Writer writer;
	if (p_body) {
		writer.record(BODY, String(p_root->get_path_to(p_body)), {}, {});
	}
	Vector<Node *> pending = { p_root };
	HashSet<ObjectID> visited;
	while (!pending.is_empty()) {
		Node *node = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		if (visited.has(node->get_instance_id()) || Object::cast_to<CanvasItem>(node)) {
			continue;
		}
		visited.insert(node->get_instance_id());
		Viewport *viewport = Object::cast_to<Viewport>(node);
		if (viewport && viewport != p_root->get_viewport()) {
			continue;
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			pending.push_back(node->get_child(i));
		}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
		if (ParticleInstancePool3D *pool = Object::cast_to<ParticleInstancePool3D>(node)) {
			TypedArray<ParticleInstance3D> instances = pool->get_instances(true);
			for (int i = 0; i < instances.size(); i++) {
				pending.push_back(Object::cast_to<ParticleInstance3D>(instances[i]));
			}
		}
#endif
		String path = node->is_inside_tree() ? String(p_root->get_path_to(node)) : "@" + uitos(node->get_instance_id());
		if (Node3D *spatial = Object::cast_to<Node3D>(node)) {
			Vector<double> values = { double(WRenderCapture::render_visible(spatial)) };
			Writer::transform(values, WRenderCapture::render_transform(spatial));
			Node *parent = node->get_parent();
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
			if (ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(node)) {
				if (particle->get_instance_pool()) {
					parent = particle->get_instance_pool();
				}
			}
#endif
			String parent_path;
			if (parent) {
				parent_path = parent->is_inside_tree() ? String(p_root->get_path_to(parent)) : "@" + uitos(parent->get_instance_id());
			}
			writer.record(SPATIAL, path, { spatial->get_class(), spatial->get_scene_file_path(), parent_path }, values);
		}
		if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(node)) {
			writer.record(GEOMETRY, path, {}, { double(geometry->get_sorting_priority()), geometry->get_sorting_offset(), double(geometry->is_sorting_use_aabb_center()) });
		}
		if (Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node)) {
			const SkeletonPose &pose = observe_skeleton(skeleton);
			Vector<double> poses;
			for (const Transform3D &transform : pose.transforms) {
				Writer::transform(poses, transform);
			}
			writer.record(SKELETON, path, pose.names, poses);
		}
		if (MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(node)) {
			if (mesh->get_mesh().is_valid()) {
				writer.record(MESH, path, { mesh->get_mesh()->get_path() }, { double(mesh->get_layer_mask()), double(mesh->get_cast_shadows_setting()), mesh->get_transparency() });
				for (int i = 0; i < mesh->get_mesh()->get_surface_count(); i++) {
					writer.material(mesh->get_active_material(i), path + ":" + itos(i));
				}
				writer.material(mesh->get_material_overlay(), path + ":overlay");
			}
		}
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
		if (ParticleInstance3D *particle = Object::cast_to<ParticleInstance3D>(node)) {
			Vector<double> values = { particle->get_birth(), double(particle->get_spawn_index()), particle->get_particle_previous_age(), particle->get_particle_lifetime(), particle->get_particle_relative_age(), particle->get_particle_angle() };
			Writer::vector(values, particle->get_particle_position());
			Writer::vector(values, particle->get_particle_velocity());
			Writer::vector(values, particle->get_particle_base_velocity());
			Writer::vector(values, particle->get_particle_size());
			Writer::vector(values, particle->get_particle_base_size());
			Writer::color(values, particle->get_particle_color());
			Writer::color(values, particle->get_particle_render_color());
			Writer::vector(values, particle->get_particle_render_orbit_offset());
			writer.record(PARTICLE, path, {}, values);
		}
#endif
		if (Camera3D *camera = Object::cast_to<Camera3D>(node)) {
			writer.record(CAMERA, path, {}, { double(camera->is_current()), double(camera->get_projection()), camera->get_fov(), camera->get_size(), camera->get_near(), camera->get_far(), double(camera->get_keep_aspect_mode()) });
		}
		if (Light3D *light = Object::cast_to<Light3D>(node)) {
			Vector<double> values = { double(light->has_shadow()), double(light->get_cull_mask()) };
			Writer::color(values, light->get_color());
			for (int i = 0; i < Light3D::PARAM_MAX; i++) {
				values.push_back(light->get_param(Light3D::Param(i)));
			}
			writer.record(LIGHT, path, {}, values);
		}
		if (Decal *decal = Object::cast_to<Decal>(node)) {
			Vector<String> textures;
			for (int i = 0; i < Decal::TEXTURE_MAX; i++) {
				Ref<Texture2D> texture = decal->get_texture(Decal::DecalTexture(i));
				textures.push_back(texture.is_valid() ? texture->get_path() : String());
			}
			Vector<double> values;
			Writer::vector(values, decal->get_size());
			Writer::color(values, decal->get_modulate());
			writer.record(DECAL, path, textures, values);
		}
		if (AudioStreamPlayer3D *audio = Object::cast_to<AudioStreamPlayer3D>(node)) {
			Ref<AudioStream> stream = audio->get_stream();
			writer.record(AUDIO, path, { stream.is_valid() ? stream->get_path() : String() }, { double(audio->is_playing()), audio->get_playback_position(), audio->get_volume_db(), audio->get_pitch_scale() });
		}
		if (AnimationPlayer *animation = Object::cast_to<AnimationPlayer>(node)) {
			const bool valid = animation->is_valid();
			writer.record(ANIMATION, path, { valid ? animation->get_assigned_animation() : StringName() },
					{ double(animation->is_playing()), valid ? animation->get_current_animation_position() : 0.0, animation->get_playing_speed(), double(valid) });
		}
		ERR_FAIL_COND_V_MSG(writer.output->get_size() > MAX_MESSAGE_BYTES - 8, PackedByteArray(), "Live snapshot exceeded its memory budget. No recording was written.");
	}
	Vector<ObjectID> removed;
	for (const KeyValue<ObjectID, SkeletonPose> &entry : skeleton_poses) {
		if (!visited.has(entry.key)) {
			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(ObjectDB::get_instance(entry.key));
			if (skeleton) {
				skeleton->disconnect(SNAME("skeleton_updated"), callable_mp(this, &WLiveSceneProbe::skeleton_updated).bind(entry.key));
			}
			removed.push_back(entry.key);
		}
	}
	for (ObjectID id : removed) {
		skeleton_poses.erase(id);
	}
	return writer.output->get_data_array();
}
#endif
