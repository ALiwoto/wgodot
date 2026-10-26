// wgodot-changes::file
#pragma once

#include "particle_motion.h"
#include "particle_environment.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/decal.h"

class ParticleShaderBindings : public Resource {
	GDCLASS(ParticleShaderBindings, Resource);

protected:
	static void _bind_methods();

public:
	StringName camera_offset = "camera_offset";
	void set_camera_offset(const StringName &p_name) { camera_offset = p_name; }
	StringName get_camera_offset() const { return camera_offset; }
	StringName mesh_pre_rotation_x = "mesh_pre_rotation_x";
	void set_mesh_pre_rotation_x(const StringName &p_name) { mesh_pre_rotation_x = p_name; }
	StringName get_mesh_pre_rotation_x() const { return mesh_pre_rotation_x; }
	StringName mesh_pre_rotation_y = "mesh_pre_rotation_y";
	void set_mesh_pre_rotation_y(const StringName &p_name) { mesh_pre_rotation_y = p_name; }
	StringName get_mesh_pre_rotation_y() const { return mesh_pre_rotation_y; }
	StringName mesh_pre_rotation_z = "mesh_pre_rotation_z";
	void set_mesh_pre_rotation_z(const StringName &p_name) { mesh_pre_rotation_z = p_name; }
	StringName get_mesh_pre_rotation_z() const { return mesh_pre_rotation_z; }
	StringName sprite_uv_flip = "sprite_uv_flip";
	void set_sprite_uv_flip(const StringName &p_name) { sprite_uv_flip = p_name; }
	StringName get_sprite_uv_flip() const { return sprite_uv_flip; }
	StringName age = "age";
	void set_age(const StringName &p_name) { age = p_name; }
	StringName get_age() const { return age; }
	StringName tint = "tint";
	void set_tint(const StringName &p_name) { tint = p_name; }
	StringName get_tint() const { return tint; }
	StringName dynamics = "dynamics";
	void set_dynamics(const StringName &p_name) { dynamics = p_name; }
	StringName get_dynamics() const { return dynamics; }
	StringName mesh_scale = "mesh_scale";
	void set_mesh_scale(const StringName &p_name) { mesh_scale = p_name; }
	StringName get_mesh_scale() const { return mesh_scale; }
	StringName angle = "angle";
	void set_angle(const StringName &p_name) { angle = p_name; }
	StringName get_angle() const { return angle; }
	StringName velocity_direction = "velocity_direction";
	void set_velocity_direction(const StringName &p_name) { velocity_direction = p_name; }
	StringName get_velocity_direction() const { return velocity_direction; }
	StringName sprite_frame = "sprite_frame";
	void set_sprite_frame(const StringName &p_name) { sprite_frame = p_name; }
	StringName get_sprite_frame() const { return sprite_frame; }
	StringName sprite_size = "sprite_size";
	void set_sprite_size(const StringName &p_name) { sprite_size = p_name; }
	StringName get_sprite_size() const { return sprite_size; }
	StringName surface_plane = "surface_plane";
	void set_surface_plane(const StringName &p_name) { surface_plane = p_name; }
	StringName get_surface_plane() const { return surface_plane; }
};

class ParticleInstance3D;
class ParticleInstancePool3D;

class ParticleSpawnContext3D : public RefCounted {
	GDCLASS(ParticleSpawnContext3D, RefCounted);
	HashMap<const ParticleBirthRule3D *, int> source_indices;
	Vector<TypedArray<ParticleInstance3D>> source_channels;
	double ground_offset = 0.0;
	Vector3 emitter_velocity;
	RayCast3D *surface_ray = nullptr;
	Vector3 surface_origin_offset;
	Vector4 surface_plane;
	bool surface_plane_changed = false;
protected:
	static void _bind_methods();
public:
	void set_source_channel(int p_channel, const TypedArray<ParticleInstance3D> &p_sources);
	void clear_source_channel(int p_channel);
	TypedArray<ParticleInstance3D> get_sources() const { return source_channels.is_empty() ? TypedArray<ParticleInstance3D>() : source_channels[0]; }
	void set_ground_offset(double p_value) { ground_offset = p_value; }
	double get_ground_offset() const { return ground_offset; }
	void set_emitter_velocity(const Vector3 &p_value) { emitter_velocity = p_value; }
	Vector3 get_emitter_velocity() const { return emitter_velocity; }
	void set_surface_ray(RayCast3D *p_value) { surface_ray = p_value; }
	RayCast3D *get_surface_ray() const { return surface_ray; }
	void set_surface_origin_offset(const Vector3 &p_value) { surface_origin_offset = p_value; }
	Vector3 get_surface_origin_offset() const { return surface_origin_offset; }
	void set_surface_plane(const Vector4 &p_value) { surface_plane_changed |= surface_plane != p_value; surface_plane = p_value; }
	Vector4 get_surface_plane() const { return surface_plane; }
	bool has_surface_plane_changed() const { return surface_plane_changed; }
	void clear_surface_plane_changed() { surface_plane_changed = false; }
	void capture_surface_plane(const Vector3 &p_position);
	void reset() { source_indices.clear(); source_channels.clear(); ground_offset = 0.0; emitter_velocity = Vector3(); surface_plane_changed = false; }
	ParticleInstance3D *sample_source(const Ref<RandomStream32> &p_random, const Ref<ParticleBirthRule3D> &p_initial);
};

class ParticleInstance3D : public Node3D {
	GDCLASS(ParticleInstance3D, Node3D);
	friend class ParticleInstancePool3D;
	ParticleInstancePool3D *instance_pool = nullptr;
	struct RenderInstance {
		VisualInstance3D *visual = nullptr;
		GeometryInstance3D *geometry = nullptr;
		AABB authored_aabb;
		bool bounds_overridden = false;
	};
	Vector<RenderInstance> render_instances;
	bool render_instances_prepared = false;
	AABB visibility_aabb;
	Vector2 state_render_size;
	double state_camera_offset = 0.0;
	void update_render_bounds(RenderInstance &p_instance, const Transform3D &p_transform);
	void collect_render_instances(Node *p_node);
	void prepare_render_instances(ParticleInstancePool3D *p_pool);
	void set_render_scenario(RID p_scenario);
	void publish_render_instances();
	void set_render_shader_parameter(const StringName &p_name, const Variant &p_value);
	Transform3D get_emitter_transform() const;

	double birth = 0.0;
	bool follows_emitter = false;
	bool source_size_clock = false;
	bool source_material_clock = false;
	double lifetime = 0.0;
	GeometryInstance3D * geometry = nullptr;
	Decal *decal = nullptr;
	Color decal_modulate = Color(1, 1, 1, 1);
	Color initial_color = Color(1, 1, 1, 1);
	Ref<Gradient> color_over_life;
	Ref<ParticleColorCurve> color_over_life_variation;
	TypedArray<ParticleColorCurve> color_scales;
	OmniLight3D * light = nullptr;
	Ref<ParticleLightProfile3D> light_parameters;
	bool advance_on_spawn = true;
	Ref<ParticleMeshProfile3D> motion;
	Ref<ParticleSpriteProfile3D> sprite_motion;
	Ref<ParticleMaterialChannels> material_parameters;
	Node3D * pose = nullptr;
	Node3D * stretch = nullptr;
	bool use_fixed_seed = false;
	int64_t random_seed = 0;
	TypedArray<ParticleVelocityRule3D> velocity_modifiers;
	Ref<ParticleFloatCurve> camera_offset;
	bool update_camera_offset = false;
	int64_t spawn_index = 0;
	double camera_offset_scale = 1.0;
	Ref<ParticleShaderBindings> shader_bindings;
	static Ref<ParticleShaderBindings> default_shader_bindings;
	ParticleShaderBindings *bindings() const;
	HashMap<int, Vector3> accelerations;
	double state_previous_age = 0.0;
	double state_relative_age = 0.0;
	double state_inverse_lifetime = 0.0;
	bool state_spawned = false;
	Vector3 state_rotation_degrees;
	Vector3 state_position;
	Vector3 state_velocity;
	Vector3 state_base_velocity;
	double state_angle = 0.0;
	Vector3 state_angular_velocity;
	double state_angular_speed = 0.0;
	Vector3 state_orbit_offset;
	Vector3 state_orbit_rotation;
	Vector3 state_orbit_rate;
	Vector3 state_base_orbit_rate;
	Vector3 state_render_orbit_offset;
	double state_last_frame_change = 0.0;
	double state_lifetime = 0.0;
	Vector3 state_size;
	Vector3 state_base_size;
	Vector4 state_material_parameters;
	Color state_color;
	Color state_render_color;
	bool state_light_enabled = false;
	double state_light_radius_multiplier = 1.0;
	Vector3 state_previous_emitter_position;
	double state_emitter_ground_offset = 0.0;
	Ref<RandomStream32> sprite_random;
	Ref<RandomStream32> color_random;
	Ref<RandomStream32> color_life_random;
	Ref<RandomStream32> size_random;
	Ref<RandomStream32> size_life_random;
	Ref<RandomStream32> location_random;
	Ref<RandomStream32> drag_random;
	Ref<RandomStream32> attraction_random;
	Ref<RandomStream32> rotation_random;
	Ref<RandomStream32> frame_random;
	Ref<RandomStream32> ground_random;
	Ref<RandomStream32> acceleration_random;
	Ref<RandomStream32> camera_random;
	Ref<RandomStream32> orientation_random;
	Vector3i random_vector_order = Vector3i(0, 1, 2);

	void reset_random_streams();
	void inherit_particle(ParticleInstance3D *p_source, const Ref<ParticleBirthRule3D> &p_initial);
	void copy_source_state(ParticleInstance3D *p_source, bool p_spawning);
	bool project_ground(ParticleGroundProjector3D *p_ground, double p_task_ratio, bool p_spawning = false);
	void set_frame(double p_frame);
	void prepare_orbit(const Ref<ParticleMotionProfile3D> &p_trajectory, double p_emitter_seconds);
	Vector3 advance_orbit(const Ref<ParticleMotionProfile3D> &p_trajectory, double p_relative_age, double p_delta, bool p_spawned_now, double p_emitter_seconds);
	void advance_sprite(double p_relative_age, double p_delta, bool p_spawned_now, double p_simulation_delta, const Ref<RandomStream32> &p_random, ParticleInstance3D *p_linked_particle, double p_emitter_seconds, double p_parameter_ratio, const Vector3 *p_birth_light_position);
	void set_light_pose_position(const Vector3 &p_position);
	void inherit_source_movement(const Ref<ParticleVectorCurve3D> &p_curve, double p_relative_age, double p_delta);
	Vector3 acceleration_in_simulation(Vector3 p_value, bool p_world_space);
	void add_acceleration(Vector3 p_value, double p_delta);
	void apply_accelerations_on_spawn(double p_age);
	void update_motion_velocity(double p_relative_age, double p_delta, double p_emitter_seconds, double p_parameter_ratio);
	void apply_point_attraction(const Ref<ParticleVelocityRule3D> &p_rule, double p_delta, double p_emitter_seconds, double p_parameter_ratio);
	void update_velocity(Vector3 p_multiplier);
	void uniform(const StringName &p_name, const Variant &p_value);

protected:
	static void _bind_methods();

public:
	ParticleInstance3D();
	// Fixed bounds are in emitter space. Empty bounds use the rendered shape.
	void set_visibility_aabb(const AABB &p_value) { visibility_aabb = p_value; }
	void set_sorting_priority(int32_t p_priority);
	AABB get_visibility_aabb() const { return visibility_aabb; }
	ParticleInstancePool3D *get_instance_pool() const { return instance_pool; }
	Transform3D get_particle_transform() const;
	Transform3D get_render_transform(Node3D *p_node) const;
	bool is_render_visible(Node3D *p_node) const;
	static void initialize_defaults();
	static void clear_defaults();
	void set_random_vector_order(const Vector3i &p_order);
	Vector3i get_random_vector_order() const { return random_vector_order; }
	void reset_playback();
	void publish_surface_plane(const Vector4 &p_plane) { uniform(bindings()->surface_plane, p_plane); }
	double remaining_seconds() const;
	bool sample_playback(double p_seconds, double p_delta, const Ref<RandomStream32> &p_random, const TypedArray<ParticleBirthRule3D> &p_initial_values, double p_spawn_age, ParticleGroundProjector3D *p_ground, Vector3 p_birth_offset, double p_parameter_ratio, double p_emitter_seconds, const TypedArray<RandomStream32> &p_initial_random, const Ref<ParticleSpawnContext3D> &p_birth_source, ParticleInstance3D *p_linked_particle);
	void set_shader_bindings(const Ref<ParticleShaderBindings> &p_value) { shader_bindings = p_value; }
	Ref<ParticleShaderBindings> get_shader_bindings() const { return shader_bindings; }
	void set_birth(double p_value) { birth = p_value; }
	double get_birth() const { return birth; }
	void set_follows_emitter(bool p_value) { follows_emitter = p_value; }
	bool get_follows_emitter() const { return follows_emitter; }
	void set_source_size_clock(bool p_value) { source_size_clock = p_value; }
	bool get_source_size_clock() const { return source_size_clock; }
	void set_source_material_clock(bool p_value) { source_material_clock = p_value; }
	bool get_source_material_clock() const { return source_material_clock; }
	void set_lifetime(double p_value) { lifetime = p_value; }
	double get_lifetime() const { return lifetime; }
	void set_geometry(GeometryInstance3D * p_value) { geometry = p_value; }
	GeometryInstance3D * get_geometry() const { return geometry; }
	void set_decal(Decal *p_value) { decal = p_value; }
	Decal *get_decal() const { return decal; }
	void set_decal_modulate(const Color &p_value) { decal_modulate = p_value; }
	Color get_decal_modulate() const { return decal_modulate; }
	void set_initial_color(const Color & p_value) { initial_color = p_value; }
	Color get_initial_color() const { return initial_color; }
	void set_color_over_life(const Ref<Gradient> & p_value) { color_over_life = p_value; }
	Ref<Gradient> get_color_over_life() const { return color_over_life; }
	void set_color_over_life_variation(const Ref<ParticleColorCurve> & p_value) { color_over_life_variation = p_value; }
	Ref<ParticleColorCurve> get_color_over_life_variation() const { return color_over_life_variation; }
	void set_color_scales(const TypedArray<ParticleColorCurve> & p_value) { color_scales = p_value; }
	TypedArray<ParticleColorCurve> get_color_scales() const { return color_scales; }
	void set_light(OmniLight3D * p_value) { light = p_value; }
	OmniLight3D * get_light() const { return light; }
	void set_light_parameters(const Ref<ParticleLightProfile3D> & p_value) { light_parameters = p_value; }
	Ref<ParticleLightProfile3D> get_light_parameters() const { return light_parameters; }
	void set_advance_on_spawn(bool p_value) { advance_on_spawn = p_value; }
	bool get_advance_on_spawn() const { return advance_on_spawn; }
	void set_motion(const Ref<ParticleMeshProfile3D> & p_value) { motion = p_value; }
	Ref<ParticleMeshProfile3D> get_motion() const { return motion; }
	void set_sprite_motion(const Ref<ParticleSpriteProfile3D> & p_value) { sprite_motion = p_value; }
	Ref<ParticleSpriteProfile3D> get_sprite_motion() const { return sprite_motion; }
	void set_material_parameters(const Ref<ParticleMaterialChannels> & p_value) { material_parameters = p_value; }
	Ref<ParticleMaterialChannels> get_material_parameters() const { return material_parameters; }
	void set_pose(Node3D * p_value) { pose = p_value; }
	Node3D * get_pose() const { return pose; }
	void set_stretch(Node3D * p_value) { stretch = p_value; }
	Node3D * get_stretch() const { return stretch; }
	void set_use_fixed_seed(bool p_value) { use_fixed_seed = p_value; }
	bool get_use_fixed_seed() const { return use_fixed_seed; }
	void set_random_seed(int64_t p_value) { random_seed = p_value; }
	int64_t get_random_seed() const { return random_seed; }
	void set_velocity_modifiers(const TypedArray<ParticleVelocityRule3D> & p_value) { velocity_modifiers = p_value; }
	TypedArray<ParticleVelocityRule3D> get_velocity_modifiers() const { return velocity_modifiers; }
	void set_camera_offset(const Ref<ParticleFloatCurve> & p_value) { camera_offset = p_value; }
	Ref<ParticleFloatCurve> get_camera_offset() const { return camera_offset; }
	void set_update_camera_offset(bool p_value) { update_camera_offset = p_value; }
	bool get_update_camera_offset() const { return update_camera_offset; }
	void set_spawn_index(int64_t p_value) { spawn_index = p_value; }
	int64_t get_spawn_index() const { return spawn_index; }
	void set_camera_offset_scale(double p_value) { camera_offset_scale = p_value; }
	double get_camera_offset_scale() const { return camera_offset_scale; }
	Ref<RandomStream32> get_sprite_random() const { return sprite_random; }
	Ref<RandomStream32> get_color_random() const { return color_random; }
	Ref<RandomStream32> get_color_life_random() const { return color_life_random; }
	Ref<RandomStream32> get_size_random() const { return size_random; }
	Ref<RandomStream32> get_size_life_random() const { return size_life_random; }
	Ref<RandomStream32> get_location_random() const { return location_random; }
	Ref<RandomStream32> get_drag_random() const { return drag_random; }
	Ref<RandomStream32> get_rotation_random() const { return rotation_random; }
	Ref<RandomStream32> get_frame_random() const { return frame_random; }
	Ref<RandomStream32> get_ground_random() const { return ground_random; }
	Ref<RandomStream32> get_acceleration_random() const { return acceleration_random; }
	Ref<RandomStream32> get_camera_random() const { return camera_random; }
	Ref<RandomStream32> get_orientation_random() const;
	double get_particle_previous_age() const { return state_previous_age; }
	double get_particle_relative_age() const { return state_relative_age; }
	double get_particle_inverse_lifetime() const { return state_inverse_lifetime; }
	bool get_particle_spawned() const { return state_spawned; }
	Vector3 get_particle_rotation_degrees() const { return state_rotation_degrees; }
	Vector3 get_particle_position() const { return state_position; }
	Vector3 get_particle_velocity() const { return state_velocity; }
	Vector3 get_particle_base_velocity() const { return state_base_velocity; }
	double get_particle_angle() const { return state_angle; }
	Vector3 get_particle_angular_velocity() const { return state_angular_velocity; }
	double get_particle_angular_speed() const { return state_angular_speed; }
	Vector3 get_particle_orbit_offset() const { return state_orbit_offset; }
	Vector3 get_particle_orbit_rotation() const { return state_orbit_rotation; }
	Vector3 get_particle_orbit_rate() const { return state_orbit_rate; }
	Vector3 get_particle_base_orbit_rate() const { return state_base_orbit_rate; }
	Vector3 get_particle_render_orbit_offset() const { return state_render_orbit_offset; }
	double get_particle_last_frame_change() const { return state_last_frame_change; }
	double get_particle_lifetime() const { return state_lifetime; }
	Vector3 get_particle_size() const { return state_size; }
	Vector3 get_particle_base_size() const { return state_base_size; }
	Vector4 get_particle_material_parameters() const { return state_material_parameters; }
	Color get_particle_color() const { return state_color; }
	Color get_particle_render_color() const { return state_render_color; }
	bool get_particle_light_enabled() const { return state_light_enabled; }
	Vector3 get_particle_previous_emitter_position() const { return state_previous_emitter_position; }
	double get_particle_emitter_ground_offset() const { return state_emitter_ground_offset; }
};
