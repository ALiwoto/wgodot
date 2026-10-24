// wgodot-changes::file
#pragma once
#include "modules/modules_enabled.gen.h"
#ifdef TOOLS_ENABLED
#include "mesh_readback.h"
#include "core/io/config_file.h"
#include "core/templates/hash_set.h"
#include "core/variant/typed_array.h"
#include "servers/rendering/storage/wgodot_instance_shader_parameters.h"

class Node3D;
class Skeleton3D;
class MeshInstance3D;
class ParticleInstance3D;

// Live scene reads finish on the scene thread. Only owned, typed values survive
// into serialization; neither nodes nor mutable resources cross that boundary.
class WSceneSnapshot : public RefCounted {
	GDCLASS(WSceneSnapshot, RefCounted);
	Ref<ConfigFile> metadata;
	Vector<String> section_order;
	HashSet<String> sections;
	void remember_section(const String &p_section);
	struct Spatial {
		String section, class_name, source_scene;
		int64_t identity = 0;
		Transform3D transform;
		bool visible = false;
	};
	struct Skeleton {
		String section;
		PackedStringArray names;
		Vector<Transform3D> poses;
	};
	struct SkeletonNames {
		uint64_t version = 0;
		PackedStringArray names;
	};
	HashMap<ObjectID, SkeletonNames> skeleton_names;
	struct Mesh {
		String section, path;
		Transform3D transform;
		uint64_t rid = 0;
		uint32_t layers = 0;
		int shadow_mode = 0;
		bool visible = false, skin = false, has_palette = false;
		double transparency = 0, visibility_begin = 0, visibility_end = 0, cull_margin = 0;
		AABB custom_aabb;
		Vector<WInstanceShaderParameters::Item> uniforms;
		PackedStringArray names;
		PackedInt32Array bones;
		Vector<Transform3D> poses, palette;
	};
	struct Transforms {
		String section;
		PackedInt64Array identities;
		PackedInt32Array births;
		PackedByteArray visible;
		Vector<Transform3D> transforms;
		bool particles = false;
	};
	struct Particle {
		String section;
		Vector4 color;
		double age = 0, lifetime = 0, relative_age = 0, angle = 0;
		bool sprite = false;
		Transform3D transform;
		Vector3 position, velocity, base_size, orbit_position, orbit_offset, orbit_rotation;
	};
	Vector<Spatial> spatials;
	Vector<Skeleton> skeletons;
	Vector<Mesh> meshes;
	Vector<Transforms> transforms;
	Vector<Particle> particles;
	struct MeshBounds {
		String section;
		Ref<WMeshBoundsReadback> readback;
	};
	Vector<MeshBounds> mesh_bounds;
	HashMap<ObjectID, Ref<WMeshBoundsReadback>> mesh_readbacks;
	HashMap<String, int> skeleton_sections;
	void write_to(ConfigFile &p_output) const;

protected:
	static void _bind_methods();

public:
	void capture_spatial(Node3D *p_node, const String &p_section);
	void capture_skeleton(Skeleton3D *p_node, const String &p_section);
	void capture_mesh(MeshInstance3D *p_node, const String &p_section, const Ref<ConfigFile> &p_palettes);
	void capture_mesh_vertex_bounds(const Ref<::Mesh> &p_mesh, const String &p_section);
	bool is_geometry_ready() const;
	void capture_transforms(const TypedArray<Node3D> &p_nodes, const String &p_section);
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	void capture_particle_transforms(const TypedArray<ParticleInstance3D> &p_nodes, const String &p_section);
	void capture_particle(ParticleInstance3D *p_node, const String &p_section);
#endif
	bool has_skeleton(const String &p_section) const { return skeleton_sections.has(p_section) || metadata->has_section_key(p_section, "bones"); }
	void set_value(const String &p_section, const String &p_key, const Variant &p_value);
	Ref<ConfigFile> get_metadata() const { return metadata; }
	const Vector<String> &get_sections() const { return section_order; }
	void clear();
	// Caller transfers exclusive ownership after sampling. Explicit script
	// fields override the generic records (for example renderer-corrected poses).
	Ref<ConfigFile> materialize();
	WSceneSnapshot() { metadata.instantiate(); }
};
#endif
