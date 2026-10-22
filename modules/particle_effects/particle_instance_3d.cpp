// wgodot-changes::file
#include "particle_instance_3d.h"

Ref<ParticleShaderBindings> ParticleInstance3D::default_shader_bindings;

void ParticleInstance3D::initialize_defaults() {
	default_shader_bindings.instantiate();
}

void ParticleInstance3D::clear_defaults() {
	default_shader_bindings.unref();
}

ParticleShaderBindings *ParticleInstance3D::bindings() const {
	return shader_bindings.is_valid() ? shader_bindings.ptr() : default_shader_bindings.ptr();
}

void ParticleSpawnContext3D::capture_surface_plane(const Vector3 &p_position) {
	surface_ray->set_global_position(p_position + surface_origin_offset);
	surface_ray->force_raycast_update();
	if (surface_ray->is_colliding()) {
		const Vector3 normal = surface_ray->get_collision_normal();
		set_surface_plane(Vector4(normal.x, normal.y, normal.z, normal.dot(surface_ray->get_collision_point())));
	}
}

void ParticleSpawnContext3D::set_source_channel(int p_channel, const TypedArray<ParticleInstance3D> &p_sources) {
	ERR_FAIL_COND(p_channel < 0);
	if (p_channel >= source_channels.size()) {
		source_channels.resize(p_channel + 1);
	}
	source_channels.write[p_channel] = p_sources;
}

void ParticleSpawnContext3D::clear_source_channel(int p_channel) {
	ERR_FAIL_COND(p_channel < 0);
	if (p_channel < source_channels.size()) {
		// Replace this handle; clearing the shared array would clear the source.
		source_channels.write[p_channel] = TypedArray<ParticleInstance3D>();
	}
}

ParticleInstance3D *ParticleSpawnContext3D::sample_source(const Ref<RandomStream32> &p_random, const Ref<ParticleBirthRule3D> &p_initial) {
	const int channel = p_initial->get_source_channel();
	if (channel >= source_channels.size()) {
		return nullptr;
	}
	const TypedArray<ParticleInstance3D> &sources = source_channels[channel];
	if (sources.is_empty()) {
		return nullptr;
	}
	int index;
	if (p_initial->get_sequential_source()) {
		const int *previous = source_indices.getptr(p_initial.ptr());
		index = (previous ? *previous : 0) + 1;
		if (index >= sources.size()) {
			index = 0;
		}
		source_indices[p_initial.ptr()] = index;
	} else {
		index = int(p_random->next_float() * (sources.size() - 0.00001));
	}
	return Object::cast_to<ParticleInstance3D>(sources[index]);
}

ParticleInstance3D::ParticleInstance3D() {
	sprite_random.instantiate();
	color_random.instantiate();
	color_life_random.instantiate();
	size_random.instantiate();
	size_life_random.instantiate();
	location_random.instantiate();
	drag_random.instantiate();
	attraction_random.instantiate();
	rotation_random.instantiate();
	frame_random.instantiate();
	ground_random.instantiate();
	acceleration_random.instantiate();
	camera_random.instantiate();
	orientation_random.instantiate();
}

Ref<RandomStream32> ParticleInstance3D::get_orientation_random() const {
	if (motion.is_valid() && motion->get_pre_orientation().is_valid() && motion->get_pre_orientation()->get_use_shared_random()) {
		return motion->get_pre_orientation()->get_random_stream();
	}
	return orientation_random;
}

void ParticleInstance3D::set_random_vector_order(const Vector3i &p_order) {
	Ref<RandomStream32> streams[] = { sprite_random, color_random, color_life_random, size_random, size_life_random, location_random, drag_random, attraction_random, rotation_random, frame_random, ground_random, acceleration_random, camera_random, orientation_random };
	for (const Ref<RandomStream32> &stream : streams) {
		stream->set_vector_order(p_order);
	}
	random_vector_order = p_order;
}

void ParticleInstance3D::reset_random_streams() {
	Ref<RandomStream32> streams[] = { color_random, color_life_random, size_random, size_life_random, location_random, drag_random, attraction_random, rotation_random, frame_random, ground_random, acceleration_random, camera_random, orientation_random };
	for (const Ref<RandomStream32> &stream : streams) {
		stream->reset(Math::rand());
	}
}

void ParticleInstance3D::reset_playback() {
	reset_random_streams();
	state_previous_age = 0.0;
	state_relative_age = 0.0;
	state_last_frame_change = 0.0;
	state_spawned = false;
	if (motion.is_valid()) {
		state_rotation_degrees = motion->get_rotation() * (180.0 / Math::PI);
		pose->set_as_top_level(false);
	}
	if (sprite_motion.is_valid()) {
		set_as_top_level(false);
		set_transform(Transform3D());
	}
	hide();
}

static Vector3 unscale_particle_vector(const Vector3 &p_value, const Basis &p_basis) {
	// A collapsed axis has no inverse coordinate. Preserve the lengths of
	// the other axes; Basis::get_scale() zeros all three at determinant zero.
	const Vector3 scale = p_basis.get_scale_abs() * (p_basis.determinant() < 0.0 ? -1.0 : 1.0);
	return Vector3(
			scale.x != 0.0 ? p_value.x / scale.x : 0.0,
			scale.y != 0.0 ? p_value.y / scale.y : 0.0,
			scale.z != 0.0 ? p_value.z / scale.z : 0.0);
}

double ParticleInstance3D::remaining_seconds() const {
	return state_lifetime <= 0.0 ? INFINITY : MAX(0.0, state_lifetime - state_previous_age);
}

void ParticleInstance3D::uniform(const StringName &p_name, const Variant &p_value) {
	if (geometry && !p_name.is_empty()) {
		geometry->set_instance_shader_parameter(p_name, p_value);
	}
}

void ParticleInstance3D::inherit_particle(ParticleInstance3D *p_source, const Ref<ParticleBirthRule3D> &p_initial) {
	Transform3D frame = get_particle_transform();
	if (!follows_emitter) {
		frame.orthonormalize();
	}
	if (p_source->follows_emitter || p_source->sprite_motion.is_valid()) {
		const Transform3D source_frame = p_source->get_particle_transform();
		if (source_frame == frame) {
			state_position = p_source->state_position;
		} else {
			// Subtract origins before adding the particle's small offset. A
			// round trip through world coordinates loses precision far from zero.
			state_position = frame.basis.inverse().xform(source_frame.basis.xform(p_source->state_position) + (source_frame.origin - frame.origin));
		}
	} else {
		state_position = frame.basis.inverse().xform(p_source->state_position - frame.origin);
	}
	Vector3 velocity = p_source->state_velocity;
	if (p_source->sprite_motion.is_valid() && !p_source->follows_emitter) {
		velocity = p_source->get_particle_transform().basis.xform(velocity);
	}
	if (!follows_emitter) {
		velocity = get_particle_transform().basis.orthonormalized().inverse().xform(velocity);
	}
	state_velocity += velocity * p_initial->get_source_velocity_scale();
	state_angle += p_source->state_angle * p_initial->get_source_rotation_scale();
}

bool ParticleInstance3D::sample_playback(double p_seconds, double p_delta, const Ref<RandomStream32> &p_random, const TypedArray<ParticleBirthRule3D> &p_initial_values, double p_spawn_age, ParticleGroundProjector3D *p_ground, Vector3 p_birth_offset, double p_parameter_ratio, double p_emitter_seconds, const TypedArray<RandomStream32> &p_initial_random, const Ref<ParticleSpawnContext3D> &p_birth_source, ParticleInstance3D *p_linked_particle) {
	if (p_seconds < birth) {
		return true;
	}
	double age = state_previous_age + p_delta;
	const bool spawned_now = !state_spawned;
	double birth_age = 0.0;
	double birth_curve_age = -1.0;
	Vector3 unlinked_birth_velocity;
	Vector3 birth_rotation_degrees;
	bool has_birth_rotation = false;
	bool ground_sampled = false;
	bool ground_failed = false;
	const Transform3D emitter = get_emitter_transform();
	if (!state_spawned) {
		state_spawned = true;
		age = advance_on_spawn ? p_seconds - birth : 0.0;
		if (p_spawn_age >= 0.0) {
			age = p_spawn_age;
		}
		birth_age = age;
		state_lifetime = lifetime;
		if (advance_on_spawn && state_lifetime > 0.0) {
			state_relative_age = float(age / state_lifetime);
		}
		if (motion.is_valid()) {
			state_size = motion->get_size();
			state_angular_speed = motion->get_spin_speed();
			state_angular_velocity = motion->get_angular_velocity();
			state_position = motion->get_position();
			state_velocity = motion->get_velocity();
			if (motion->get_initial_velocity().is_valid()) {
				state_velocity += motion->get_initial_velocity()->sample_velocity(p_random, get_particle_transform().basis, follows_emitter);
			}
		} else if (sprite_motion.is_valid()) {
			state_size = sprite_motion->get_size();
			state_angular_speed = sprite_motion->get_angular_velocity();
			state_position = sprite_motion->get_position();
			state_velocity = sprite_motion->get_velocity();
			if (sprite_motion->get_initial_velocity().is_valid()) {
				state_velocity += sprite_motion->get_initial_velocity()->sample_velocity(p_random, get_particle_transform().basis, follows_emitter);
			}
		}
		state_angle = sprite_motion.is_valid() ? sprite_motion->get_rotation() : 0.0;
		if (motion.is_valid()) {
			state_angle = motion->get_spin_angle();
		}
		state_base_size = state_size;
		int frame = 0;
		state_material_parameters = Vector4();
		state_emitter_ground_offset = p_birth_source.is_valid() ? p_birth_source->get_ground_offset() : 0.0;
		state_position.y += state_emitter_ground_offset;
		state_color = initial_color;
		accelerations.clear();
		Node3D *projector = decal ? static_cast<Node3D *>(decal) : geometry;
		for (int index = 0; index < p_initial_values.size(); ++index) {
			const Ref<ParticleBirthRule3D> initial = p_initial_values[index];
			Ref<RandomStream32> birth_random = p_random;
			if (initial->get_random_seed().is_valid()) {
				birth_random = p_initial_random[index];
			}
			using Target = ParticleBirthRule3D::Target;
			const Target target = Target(initial->get_target());
			ERR_FAIL_COND_V_MSG(!projector && (target == Target::GeometryPosition || target == Target::GeometryRotation || target == Target::GeometryScale), false,
					vformat("Particle '%s' has a geometry birth rule but no geometry or decal binding.", get_name()));
			switch (target) {
				case Target::GeometryPosition:
					projector->set_position(initial->sample_vector(orientation_random, p_emitter_seconds));
					break;
				case Target::GeometryRotation: {
					const Basis rotation(initial->get_rotation_axis(), initial->sample_scalar(orientation_random, p_emitter_seconds));
					projector->set_basis(rotation.scaled_local(projector->get_scale()));
				} break;
				case Target::GeometryScale:
					projector->set_scale(initial->sample_vector(orientation_random, p_emitter_seconds));
					break;
				case Target::Ground:
					if (!p_ground->get_targets_emitter()) {
						ground_sampled = true;
						ground_failed = !project_ground(p_ground, p_parameter_ratio, true);
					}
					break;
				case Target::SourceParticle:
					if (p_linked_particle) {
						birth_curve_age = state_relative_age;
						unlinked_birth_velocity = state_velocity;
						copy_source_state(p_linked_particle, true);
					}
					break;
				case Target::SurfacePlane:
					if (p_birth_source->get_surface_ray()) {
						Transform3D particle_transform = get_particle_transform();
						if (!follows_emitter) {
							particle_transform.orthonormalize();
						}
						p_birth_source->capture_surface_plane(particle_transform.xform(state_position));
						publish_surface_plane(p_birth_source->get_surface_plane());
					}
					break;
				case Target::Acceleration: {
					Vector3 acceleration = initial->sample_vector(acceleration_random, p_emitter_seconds);
					if (initial->get_acceleration()->get_apply_owner_scale()) {
						acceleration *= get_particle_transform().basis.get_scale();
					}
					accelerations[initial->get_acceleration()->get_index()] = acceleration;
				} break;
				case Target::Light:
					state_light_radius_multiplier = light_parameters->get_radius_over_emission().is_valid() ? light_parameters->get_radius_over_emission()->sample(p_emitter_seconds, p_parameter_ratio, birth_random) : 1.0;
					state_light_enabled = birth_random->next_float() < light_parameters->get_spawn_fraction();
					break;
				case Target::EmitterPoint: {
					ParticleInstance3D *source = p_birth_source->sample_source(birth_random, initial);
					if (source) {
						inherit_particle(source, initial);
					}
				} break;
				case Target::Lifetime:
					state_lifetime += initial->sample_scalar(birth_random, p_emitter_seconds);
					// A later lifetime rule cannot revive an already expired birth.
					if (advance_on_spawn && state_relative_age <= 1.0) {
						state_relative_age = state_lifetime > 0.0 ? float(age / state_lifetime) : 0.0;
					}
					break;
				case Target::Size:
					state_size = initial->sample_vector(birth_random, p_emitter_seconds);
					state_base_size = state_size * initial->sample_texture_sign(birth_random);
					break;
				case Target::Position:
					state_position += initial->sample_vector(birth_random, p_emitter_seconds);
					break;
				case Target::WorldOffset: {
					const Basis basis = follows_emitter ? emitter.basis : emitter.basis.orthonormalized();
					state_position += basis.inverse().xform(initial->sample_vector(birth_random, p_emitter_seconds));
				} break;
				case Target::Alpha:
					state_color.a = initial->sample_scalar(birth_random, p_emitter_seconds);
					break;
				case Target::Color: {
					const Vector3 color = initial->sample_vector(birth_random, p_emitter_seconds);
					const Vector3i order = initial->get_color_order();
					state_color.r = color[order.x];
					state_color.g = color[order.y];
					state_color.b = color[order.z];
				} break;
				case Target::Rotation:
					state_angle = initial->sample_scalar(birth_random, p_emitter_seconds);
					break;
				case Target::SpriteFrame:
					frame = int(initial->sample_scalar(birth_random, p_emitter_seconds));
					state_last_frame_change = state_relative_age;
					break;
				case Target::MeshRotation:
					state_rotation_degrees = initial->sample_vector(birth_random, p_emitter_seconds) * (180.0 / Math::PI);
					birth_rotation_degrees += state_rotation_degrees;
					has_birth_rotation = true;
					break;
				case Target::MeshAngularVelocity:
					state_angular_velocity = initial->sample_vector(birth_random, p_emitter_seconds);
					break;
				case Target::AngularVelocity:
					state_angular_speed = initial->sample_scalar(birth_random, p_emitter_seconds);
					break;
				case Target::Velocity:
					state_velocity += initial->sample_velocity(birth_random, get_particle_transform().basis, follows_emitter, p_emitter_seconds);
					break;
				case Target::ParentVelocity:
					state_velocity += initial->sample_parent_velocity(birth_random, get_particle_transform().basis, follows_emitter, p_birth_source.is_valid() ? p_birth_source->get_emitter_velocity() : Vector3(), p_emitter_seconds);
					break;
				case Target::Shape: {
					const Ref<ParticleSpawnShape3D> shape = initial->get_shape();
					const ParticleSpawnShape3D::Sample spawn = shape->sample(birth_random, p_emitter_seconds, spawn_index);
					state_position += shape->get_basis().xform(spawn.position);
					if (shape->get_velocity()) {
						state_velocity += shape->get_basis().xform(shape->velocity_at(spawn.velocity_offset, birth_random, p_emitter_seconds));
					}
				} break;
				case Target::MaterialParameter:
					state_material_parameters[initial->get_material_channel()] = initial->sample_scalar(birth_random, p_emitter_seconds);
					break;
				default:
					break;
			}
		}
		if (camera_offset.is_valid()) {
			double offset = camera_offset->sample(state_relative_age, p_parameter_ratio, camera_random);
			if (!follows_emitter) {
				const Vector3 scale = get_particle_transform().basis.get_scale_abs();
				offset = float(offset * scale[scale.max_axis_index()]);
			}
			uniform(bindings()->camera_offset, offset * camera_offset_scale);
		}
		if (motion.is_valid()) {
			const bool inherit_rotation = follows_emitter ? motion->get_inherit_parent_rotation() : motion->get_inherit_emitter_rotation();
			if (inherit_rotation) {
				const Vector3 parent_rotation = get_particle_transform().basis.orthonormalized().get_euler(EulerOrder::YZX) * (180.0 / Math::PI);
				state_rotation_degrees += parent_rotation;
				birth_rotation_degrees += parent_rotation;
			}
			pose->set_as_top_level(!follows_emitter);
			if (!follows_emitter) {
				state_position = emitter.orthonormalized().xform(state_position);
				state_velocity = emitter.basis.orthonormalized().xform(state_velocity);
			}
			if (motion->get_pre_orientation().is_valid()) {
				const Basis basis = Basis::from_euler(motion->get_pre_orientation()->sample(get_orientation_random()), EulerOrder::YZX);
				geometry->set_basis(basis);
				if (motion->get_faces_camera() || !motion->get_flattened_axes().is_empty()) {
					uniform(bindings()->mesh_pre_rotation_x, basis.get_column(0));
					uniform(bindings()->mesh_pre_rotation_y, basis.get_column(1));
					uniform(bindings()->mesh_pre_rotation_z, basis.get_column(2));
				}
			}
		}
		if (sprite_motion.is_valid()) {
			uniform(bindings()->sprite_uv_flip, Vector2(double(state_base_size.x < 0.0), double(state_base_size.z < 0.0)));
			state_previous_emitter_position = emitter.origin;
			if (sprite_motion->get_inherit_parent_rotation() && !follows_emitter) {
				const Basis basis = get_particle_transform().basis.orthonormalized();
				state_angle += Math::atan2(double(basis.get_column(0).z), double(basis.get_column(0).x));
			}
			if (geometry) {
				set_frame(frame);
			}
			if (!follows_emitter) {
				const Transform3D frozen_transform = get_particle_transform();
				set_as_top_level(true);
				if (instance_pool) {
					set_transform(frozen_transform);
				}
				state_position = unscale_particle_vector(state_position, frozen_transform.basis);
				state_velocity = unscale_particle_vector(state_velocity, frozen_transform.basis);
			}
			state_base_velocity = state_velocity;
		}
		const Ref<ParticleMotionProfile3D> orbit_motion = motion.is_valid() ? Ref<ParticleMotionProfile3D>(motion) : Ref<ParticleMotionProfile3D>(sprite_motion);
		prepare_orbit(orbit_motion, p_emitter_seconds);
		state_inverse_lifetime = state_lifetime > 0.0 ? float(1.0 / state_lifetime) : 0.0;
		state_base_velocity = state_velocity;
		if (birth_curve_age >= 0.0) {
			state_base_velocity = unlinked_birth_velocity;
			if (!follows_emitter) {
				state_base_velocity = motion.is_valid() ? get_particle_transform().basis.orthonormalized().xform(unlinked_birth_velocity) : unscale_particle_vector(unlinked_birth_velocity, get_particle_transform().basis);
			}
		}
		apply_accelerations_on_spawn(age);
		state_previous_emitter_position = emitter.origin;
		// Tick projection also runs on the birth frame, after all birth rules.
		// A later location rule may have moved the particle since spawn projection.
		if (p_ground && !p_ground->get_targets_emitter() && (ground_failed || ((!ground_sampled || p_ground->get_update_on_tick()) && !project_ground(p_ground, p_parameter_ratio)))) {
			state_relative_age = 1.1;
			state_previous_age = state_lifetime * 1.1;
			hide();
			publish_render_instances();
			return false;
		}
		if (!follows_emitter && p_birth_offset != Vector3()) {
			state_position += motion.is_valid() ? p_birth_offset : get_particle_transform().basis.inverse().xform(p_birth_offset);
		}
	} else {
		state_size = state_size.abs();
		if (p_ground) {
			if (p_ground->get_targets_emitter()) {
				state_emitter_ground_offset = p_birth_source->get_ground_offset();
			} else if (p_ground->get_update_on_tick()) {
				project_ground(p_ground, p_parameter_ratio);
			}
		}
	}
	// Expiry is tested before this update advances normalized age. A particle
	// remains renderable on the update that crosses its lifetime.
	set_visible(age >= 0.0 && state_relative_age <= 1.0);
	if (spawned_now) {
		if (state_relative_age <= 1.0 && birth_curve_age < 0.0) {
			state_relative_age = float(age * state_inverse_lifetime);
		}
		if (state_relative_age > 1.0) {
			hide();
		}
		if (material_parameters.is_valid()) {
			const double parameter_age = birth_curve_age >= 0.0 && !source_material_clock ? birth_curve_age : state_relative_age;
			state_material_parameters = material_parameters->spawn_values(parameter_age, state_material_parameters, p_parameter_ratio, p_emitter_seconds, state_velocity);
		}
	} else {
		state_relative_age = float(state_relative_age + double(float(p_delta * state_inverse_lifetime)));
	}
	age = state_inverse_lifetime > 0.0 ? state_relative_age / state_inverse_lifetime : 0.0;
	state_previous_age = age;
	if (is_visible()) {
		const double relative_age = birth_curve_age >= 0.0 ? birth_curve_age : state_relative_age;
		if (update_camera_offset && !spawned_now) {
			uniform(bindings()->camera_offset, camera_offset->sample(relative_age, p_parameter_ratio, camera_random) * camera_offset_scale);
		}
		Color tint = state_color;
		if (color_over_life.is_valid()) {
			tint = color_over_life->get_color_at_offset(relative_age);
		}
		if (color_over_life_variation.is_valid()) {
			tint = color_over_life_variation->sample(relative_age, color_life_random, p_parameter_ratio);
		}
		for (int index = 0; index < color_scales.size(); ++index) {
			const Ref<ParticleColorCurve> color_scale = color_scales[index];
			tint *= color_scale->sample(relative_age, color_random, p_parameter_ratio);
		}
		state_render_color = tint;
		if (decal) {
			decal->set_modulate(tint * decal_modulate);
		}
		uniform(bindings()->age, age);
		uniform(bindings()->tint, Vector4(tint.r, tint.g, tint.b, tint.a));
		if (material_parameters.is_valid() && !source_material_clock) {
			uniform(bindings()->dynamics, material_parameters->sample(relative_age, state_material_parameters, p_parameter_ratio, p_random, spawned_now, p_emitter_seconds, state_velocity));
		}
		if (motion.is_valid()) {
			if (!spawned_now) {
				state_velocity = state_base_velocity;
				update_motion_velocity(relative_age, p_delta, p_emitter_seconds, p_parameter_ratio);
			}
			if (motion->get_source_movement().is_valid()) {
				inherit_source_movement(motion->get_source_movement(), relative_age, p_delta);
			}
			if (p_linked_particle && !spawned_now) {
				copy_source_state(p_linked_particle, false);
			}
			const double birth_step = birth_age + (advance_on_spawn ? p_delta : 0.0);
			if (motion->get_position_over_life().is_valid() && !p_linked_particle) {
				Vector3 next = motion->get_position_over_life()->sample(relative_age, location_random);
				next.y += state_emitter_ground_offset;
				if (!follows_emitter) {
					next = emitter.xform(next);
				}
				next += motion->get_position_offset();
				if (!spawned_now) {
					state_velocity = p_delta > 0.0 ? (next - state_position) * motion->direct_velocity_at(relative_age) / p_delta : Vector3();
				}
				state_position = next;
				if (spawned_now) {
					state_position += state_velocity * birth_step;
				}
			} else {
				state_position += state_velocity * (spawned_now ? birth_step : p_delta);
			}
			Vector3 location = state_position;
			Vector3 size = motion->scale_at(source_size_clock ? state_relative_age : relative_age, state_size, size_random, size_life_random, spawned_now, state_velocity);
			if (!follows_emitter) {
				size *= emitter.basis.get_scale();
			}
			location += advance_orbit(motion, relative_age, p_delta, spawned_now, p_emitter_seconds);
			// Birth rotation rules accumulate in the initial pose. The last rule
			// initializes the angular state used by subsequent simulation steps.
			Vector3 angles = (has_birth_rotation ? birth_rotation_degrees : state_rotation_degrees) * (Math::PI / 180.0);
			if (motion->get_align_to_velocity() && state_base_velocity.length_squared() > 0.0) {
				angles += motion->velocity_rotation(state_base_velocity);
			}
			pose->set_transform(Transform3D(Basis::from_euler(angles, EulerOrder::YZX), location));
			stretch->set_scale(size);
			if (motion->get_faces_camera()) {
				uniform(bindings()->mesh_scale_sign, size.sign());
			}
			if (motion->get_spin_over_life().is_valid()) {
				const double spin_scale = motion->get_spin_over_life()->sample(relative_age, 0.0, rotation_random);
				state_angle += state_angular_speed * spin_scale * (spawned_now ? birth_age : p_delta);
				Vector3 direction = state_velocity;
				if (follows_emitter) {
					direction = emitter.basis.inverse().transposed().xform(direction);
				}
				uniform(bindings()->angle, state_angle);
				uniform(bindings()->velocity_direction, direction.normalized());
			}
			// Rotation velocity affects the next pose, after this pose is published.
			const Vector3 angular_velocity = motion->angular_velocity_at(relative_age, state_angular_velocity) * (180.0 / Math::PI);
			state_rotation_degrees += angular_velocity * p_delta;
		} else if (sprite_motion.is_valid()) {
			advance_sprite(relative_age, spawned_now ? birth_age : p_delta, spawned_now, p_delta, p_random, p_linked_particle, p_emitter_seconds, p_parameter_ratio);
		}
		if (material_parameters.is_valid() && source_material_clock) {
			uniform(bindings()->dynamics, material_parameters->sample(state_relative_age, state_material_parameters, p_parameter_ratio, p_random, spawned_now, p_emitter_seconds, state_velocity));
		}
		if (light) {
			light->set_visible(state_light_enabled);
			light_parameters->apply(light, relative_age, tint, sprite_motion->size_at(relative_age, state_size, size_random, size_life_random, spawned_now, state_velocity), emitter.basis.get_scale(), p_parameter_ratio, color_random, state_light_radius_multiplier);
		}
	}
	if (p_linked_particle) {
		state_previous_age = state_inverse_lifetime > 0.0 ? state_relative_age / state_inverse_lifetime : 0.0;
		uniform(bindings()->age, state_previous_age);
	}
	publish_render_instances();
	return is_visible() || age < 0.0;
}
