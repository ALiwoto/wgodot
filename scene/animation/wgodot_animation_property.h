// wgodot-changes::file
#pragma once

#include "core/object/object.h"
#include "core/string/node_path.h"

// Native animation access is separate from Object's public property interface.
// Resolve once per mixer cache; descriptors contain code, never instance state.
struct WGodotAnimationProperty {
	Variant (*read)(Object *p_target);
	void (*write)(Object *p_target, const Variant &p_value);
	// Nested Resource tracks still report the animated Resource to the
	// postprocessing hook, while cache lifetime follows the owning Node.
	ObjectID (*target_id)(Object *p_target) = nullptr;

	using Resolver = const WGodotAnimationProperty *(*)(Object *p_target);
	static void register_path(const NodePath &p_property_path, Resolver p_resolver);
	static Resolver find(const NodePath &p_track_path);
	static void clear();
};
