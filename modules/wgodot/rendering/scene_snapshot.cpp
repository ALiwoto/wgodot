// wgodot-changes::file
#include "scene_snapshot.h"
#ifdef TOOLS_ENABLED
#include "render_capture.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/skin.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_3d.h"
#endif

namespace {
TypedArray<Transform3D> boxed(const Vector<Transform3D> &p_values) {
	TypedArray<Transform3D> result;
	result.resize(p_values.size());
	for (int i = 0; i < p_values.size(); i++) {
		result[i] = p_values[i];
	}
	return result;
}
}

void WSceneSnapshot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "section", "key", "value"), &WSceneSnapshot::set_value);
	ClassDB::bind_method(D_METHOD("get_metadata"), &WSceneSnapshot::get_metadata);
	ClassDB::bind_method(D_METHOD("has_skeleton", "section"), &WSceneSnapshot::has_skeleton);
	ClassDB::bind_method(D_METHOD("capture_mesh_vertex_bounds", "mesh", "section"), &WSceneSnapshot::capture_mesh_vertex_bounds);
	ClassDB::bind_method(D_METHOD("is_geometry_ready"), &WSceneSnapshot::is_geometry_ready);
	ClassDB::bind_method(D_METHOD("clear"), &WSceneSnapshot::clear);
}

void WSceneSnapshot::remember_section(const String &p_section) {
	if (!sections.has(p_section)) {
		sections.insert(p_section);
		section_order.push_back(p_section);
	}
}

void WSceneSnapshot::set_value(const String &p_section, const String &p_key, const Variant &p_value) {
	remember_section(p_section);
	metadata->set_value(p_section, p_key, p_value);
}

void WSceneSnapshot::capture_spatial(Node3D *p_node, const String &p_section) {
	remember_section(p_section);
	Spatial value;
	value.section = p_section;
	value.class_name = p_node->get_class();
	value.identity = int64_t(p_node->get_instance_id());
	value.transform = WRenderCapture::render_transform(p_node);
	value.visible = WRenderCapture::render_visible(p_node);
	value.source_scene = p_node->get_scene_file_path();
	spatials.push_back(std::move(value));
}

void WSceneSnapshot::capture_skeleton(Skeleton3D *p_node, const String &p_section) {
	remember_section(p_section);
	Skeleton value;
	value.section = p_section;
	const int count = p_node->get_bone_count();
	SkeletonNames &names = skeleton_names[p_node->get_instance_id()];
	if (names.version != p_node->get_version()) {
		// Bone names are structural, unlike poses. Reuse their immutable COW
		// array across frames; adding, clearing or renaming bones changes version.
		PackedStringArray current;
		current.resize(count);
		String *data = current.ptrw();
		for (int i = 0; i < count; i++) {
			data[i] = p_node->get_bone_name(i);
		}
		names.names = std::move(current);
		names.version = p_node->get_version();
	}
	value.names = names.names;
	value.poses.resize(count);
	Transform3D *poses = value.poses.ptrw();
	for (int i = 0; i < count; i++) {
		poses[i] = p_node->get_bone_global_pose(i);
	}
	const int *existing = skeleton_sections.getptr(p_section);
	if (existing) {
		skeletons.write[*existing] = std::move(value);
	} else {
		skeleton_sections.insert(p_section, skeletons.size());
		skeletons.push_back(std::move(value));
	}
}

void WSceneSnapshot::capture_mesh(MeshInstance3D *p_node, const String &p_section, const Ref<ConfigFile> &p_palettes) {
	remember_section(p_section);
	Mesh value;
	value.section = p_section;
	value.visible = WRenderCapture::render_visible(p_node);
	value.transform = WRenderCapture::render_transform(p_node);
	value.layers = p_node->get_layer_mask();
	value.path = p_node->get_mesh()->get_path();
	value.rid = p_node->get_mesh()->get_rid().get_id();
	value.shadow_mode = p_node->get_cast_shadows_setting();
	value.transparency = p_node->get_transparency();
	value.visibility_begin = p_node->get_visibility_range_begin();
	value.visibility_end = p_node->get_visibility_range_end();
	value.cull_margin = p_node->get_extra_cull_margin();
	value.custom_aabb = p_node->get_custom_aabb();
	value.uniforms = p_node->get_instance_shader_parameters()->snapshot();
	Ref<Skin> skin = p_node->get_skin();
	value.skin = skin.is_valid();
	if (value.skin) {
		const int count = skin->get_bind_count();
		Ref<SkinReference> reference = p_node->get_skin_reference();
		if (reference.is_valid()) {
			String key = uitos(reference->get_skeleton().get_id());
			ERR_FAIL_COND_MSG(p_palettes.is_null() || !p_palettes->has_section_key(key, "poses"), "Skin palette missing from frozen snapshot.");
			TypedArray<Transform3D> palette = p_palettes->get_value(key, "poses");
			ERR_FAIL_COND_MSG(palette.size() != count, "Rendered skin palette and snapshot bindings differ.");
			value.palette.resize(count);
			Transform3D *poses = value.palette.ptrw();
			for (int i = 0; i < count; i++) {
				poses[i] = palette[i];
			}
			value.has_palette = true;
		}
		value.names.resize(count);
		value.bones.resize(count);
		value.poses.resize(count);
		Transform3D *poses = value.poses.ptrw();
		for (int i = 0; i < count; i++) {
			value.names.set(i, skin->get_bind_name(i));
			value.bones.set(i, skin->get_bind_bone(i));
			poses[i] = skin->get_bind_pose(i);
		}
	}
	meshes.push_back(std::move(value));
}

void WSceneSnapshot::capture_transforms(const TypedArray<Node3D> &p_nodes, const String &p_section) {
	remember_section(p_section);
	Transforms value;
	value.section = p_section;
	const int count = p_nodes.size();
	value.identities.resize(count);
	value.transforms.resize(count);
	value.visible.resize(count);
	Transform3D *poses = value.transforms.ptrw();
	for (int i = 0; i < count; i++) {
		Node3D *node = Object::cast_to<Node3D>(p_nodes[i]);
		ERR_FAIL_NULL(node);
		value.identities.set(i, int64_t(node->get_instance_id()));
		poses[i] = node->get_global_transform_interpolated();
		value.visible.set(i, node->is_visible_in_tree());
	}
	transforms.push_back(std::move(value));
}

#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
void WSceneSnapshot::capture_particle_transforms(const TypedArray<ParticleInstance3D> &p_nodes, const String &p_section) {
	remember_section(p_section);
	Transforms value;
	value.section = p_section;
	value.particles = true;
	const int count = p_nodes.size();
	value.identities.resize(count);
	value.births.resize(count);
	value.transforms.resize(count);
	value.visible.resize(count);
	Transform3D *poses = value.transforms.ptrw();
	for (int i = 0; i < count; i++) {
		ParticleInstance3D *node = Object::cast_to<ParticleInstance3D>(p_nodes[i]);
		ERR_FAIL_NULL(node);
		value.identities.set(i, int64_t(node->get_instance_id()));
		value.births.set(i, node->get_spawn_index());
		poses[i] = node->get_render_transform(node);
		value.visible.set(i, node->is_render_visible(node));
	}
	transforms.push_back(std::move(value));
}

void WSceneSnapshot::capture_particle(ParticleInstance3D *p_node, const String &p_section) {
	remember_section(p_section);
	Particle value;
	value.section = p_section;
	Color color = p_node->get_particle_render_color();
	value.color = Vector4(color.r, color.g, color.b, color.a);
	value.age = p_node->get_particle_previous_age();
	value.lifetime = p_node->get_particle_lifetime();
	value.sprite = p_node->get_sprite_motion().is_valid();
	if (value.sprite) {
		value.relative_age = p_node->get_particle_relative_age();
		value.transform = p_node->get_particle_transform();
		value.position = p_node->get_particle_position();
		value.velocity = p_node->get_particle_velocity();
		value.base_size = p_node->get_particle_base_size();
		value.angle = p_node->get_particle_angle();
	}
	value.orbit_position = p_node->get_particle_render_orbit_offset();
	value.orbit_offset = p_node->get_particle_orbit_offset();
	value.orbit_rotation = p_node->get_particle_orbit_rotation();
	particles.push_back(std::move(value));
}
#endif

void WSceneSnapshot::write_to(ConfigFile &p_output) const {
	for (const Spatial &v : spatials) {
		p_output.set_value(v.section, "class", v.class_name);
		p_output.set_value(v.section, "instance_id", v.identity);
		p_output.set_value(v.section, "transform", v.transform);
		p_output.set_value(v.section, "visible", v.visible);
		if (!v.source_scene.is_empty() || !p_output.has_section_key(v.section, "source_scene")) {
			p_output.set_value(v.section, "source_scene", v.source_scene);
		}
	}
	for (const Skeleton &v : skeletons) {
		p_output.set_value(v.section, "bones", v.names);
		p_output.set_value(v.section, "poses", boxed(v.poses));
	}
	for (const Mesh &v : meshes) {
		p_output.set_value(v.section, "visible", v.visible);
		p_output.set_value(v.section, "transform", v.transform);
		p_output.set_value(v.section, "layers", v.layers);
		p_output.set_value(v.section, "mesh", v.path);
		p_output.set_value(v.section, "mesh_rid", int64_t(v.rid));
		p_output.set_value(v.section, "cast_shadows", v.shadow_mode != GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		p_output.set_value(v.section, "two_sided_shadow", v.shadow_mode == GeometryInstance3D::SHADOW_CASTING_SETTING_DOUBLE_SIDED);
		p_output.set_value(v.section, "shadow_mode", v.shadow_mode);
		p_output.set_value(v.section, "transparency", v.transparency);
		p_output.set_value(v.section, "visibility_begin", v.visibility_begin);
		p_output.set_value(v.section, "visibility_end", v.visibility_end);
		p_output.set_value(v.section, "extra_cull_margin", v.cull_margin);
		p_output.set_value(v.section, "custom_aabb", v.custom_aabb);
		for (const auto &uniform : v.uniforms) {
			if (uniform.gpu_index >= 0) {
				p_output.set_value(v.section, "instance/" + String(uniform.info.name), uniform.value.to_variant());
			}
		}
		if (v.skin) {
			if (v.has_palette) {
				p_output.set_value(v.section, "skin/palette", boxed(v.palette));
			}
			p_output.set_value(v.section, "skin/names", v.names);
			p_output.set_value(v.section, "skin/bones", v.bones);
			p_output.set_value(v.section, "skin/poses", boxed(v.poses));
		}
	}
	for (const Transforms &v : transforms) {
		const String prefix = v.particles ? "particle_" : "";
		p_output.set_value(v.section, prefix + "identities", v.identities);
		p_output.set_value(v.section, prefix + "transforms", boxed(v.transforms));
		p_output.set_value(v.section, prefix + "visible", v.visible);
		if (v.particles) {
			p_output.set_value(v.section, "particle_births", v.births);
		}
	}
	for (const Particle &v : particles) {
		p_output.set_value(v.section, "particle/age", v.age);
		p_output.set_value(v.section, "particle/color", v.color);
		p_output.set_value(v.section, "particle/lifetime", v.lifetime);
		if (v.sprite) {
			p_output.set_value(v.section, "particle/relative_age", v.relative_age);
			p_output.set_value(v.section, "particle/transform", v.transform);
			p_output.set_value(v.section, "particle/position", v.position);
			p_output.set_value(v.section, "particle/velocity", v.velocity);
			p_output.set_value(v.section, "particle/base_size", v.base_size);
			p_output.set_value(v.section, "particle/angle", v.angle);
		}
		p_output.set_value(v.section, "particle/orbit_position", v.orbit_position);
		p_output.set_value(v.section, "particle/orbit_offset", v.orbit_offset);
		p_output.set_value(v.section, "particle/orbit_rotation", v.orbit_rotation);
	}
}

void WSceneSnapshot::capture_mesh_vertex_bounds(const Ref<::Mesh> &p_mesh, const String &p_section) {
	ERR_FAIL_COND(p_mesh.is_null());
	remember_section(p_section);
	const ObjectID identity = p_mesh->get_instance_id();
	Ref<WMeshBoundsReadback> *readback = mesh_readbacks.getptr(identity);
	if (!readback) {
		readback = &mesh_readbacks.insert(identity, WMeshBoundsReadback::create(p_mesh))->value;
	}
	mesh_bounds.push_back({ p_section, *readback });
}

bool WSceneSnapshot::is_geometry_ready() const {
	for (const KeyValue<ObjectID, Ref<WMeshBoundsReadback>> &entry : mesh_readbacks) {
		if (!entry.value->ready()) {
			return false;
		}
	}
	return true;
}

Ref<ConfigFile> WSceneSnapshot::materialize() {
	// The caller waits by yielding game frames, never by blocking the scene
	// thread. Decode only owned bytes; the live mesh can already have changed.
	ERR_FAIL_COND_V(!is_geometry_ready(), Ref<ConfigFile>());
	for (const MeshBounds &value : mesh_bounds) {
		AABB bounds;
		const Error error = value.readback->get_bounds(bounds);
		ERR_FAIL_COND_V_MSG(error != OK, Ref<ConfigFile>(), "Cannot decode recorded mesh geometry at " + value.section + ": " + error_names[error]);
		metadata->set_value(value.section, "mesh/vertex_bounds", bounds);
	}
	mesh_bounds.clear();
	mesh_readbacks.clear();
	// Preserve insertion order, including the frame/rig adjacency consumed by
	// streaming timeline readers. Record categories determine storage, not order.
	for (const String &section : metadata->get_sections()) {
		remember_section(section);
	}
	if (spatials.is_empty() && skeletons.is_empty() && meshes.is_empty() && transforms.is_empty() && particles.is_empty()) {
		return metadata;
	}
	Ref<ConfigFile> combined;
	combined.instantiate();
	write_to(*combined.ptr());
	for (const String &section : metadata->get_sections()) {
		for (const String &key : metadata->get_section_keys(section)) {
			combined->set_value(section, key, metadata->get_value(section, key));
		}
	}
	metadata = combined;
	spatials.clear();
	skeletons.clear();
	meshes.clear();
	transforms.clear();
	particles.clear();
	skeleton_sections.clear();
	skeleton_names.clear();
	return metadata;
}

void WSceneSnapshot::clear() {
	metadata->clear();
	section_order.clear();
	sections.clear();
	spatials.clear();
	skeletons.clear();
	meshes.clear();
	transforms.clear();
	particles.clear();
	skeleton_sections.clear();
	skeleton_names.clear();
	mesh_bounds.clear();
	mesh_readbacks.clear();
}
#endif
