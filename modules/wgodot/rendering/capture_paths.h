// wgodot-changes::file
#pragma once
#include "modules/modules_enabled.gen.h"
#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"
#include "core/object/gdvirtual.gen.h"
#include "scene/main/node.h"
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
#include "modules/particle_effects/particle_instance_pool_3d.h"
#endif

// Snapshot-local identities. Private pooled particles use logical birth names;
// ordinary nodes retain Godot paths. No cached node outlives its held snapshot.
class WCapturePaths : public RefCounted {
	GDCLASS(WCapturePaths, RefCounted);
	ObjectID cache_root;
	HashMap<ObjectID, String> paths;
	HashMap<ObjectID, String> pool_names;
	String build(Node *p_root, Node *p_node);

protected:
	static void _bind_methods();
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	GDVIRTUAL1RC(String, _particle_pool_name, ParticleInstancePool3D *);
#endif

public:
	void begin(Node *p_root);
	void finish();
	String get_path(Node *p_root, Node *p_node);
};
#endif
