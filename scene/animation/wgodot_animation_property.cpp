// wgodot-changes::file
#include "wgodot_animation_property.h"

#include "core/templates/hash_map.h"

namespace {
// Written only at module initialization/shutdown. Preparation workers read it
// after registration, so playback needs neither a registry lock nor a mutation.
HashMap<NodePath, WGodotAnimationProperty::Resolver> property_paths;
}

void WGodotAnimationProperty::register_path(const NodePath &p_property_path, Resolver p_resolver) {
	ERR_FAIL_COND(p_property_path.get_name_count() != 0 || p_property_path.get_subname_count() == 0);
	ERR_FAIL_COND(property_paths.has(p_property_path));
	property_paths.insert(p_property_path, p_resolver);
}

WGodotAnimationProperty::Resolver WGodotAnimationProperty::find(const NodePath &p_track_path) {
	if (property_paths.is_empty()) {
		return nullptr;
	}
	const NodePath property_path(Vector<StringName>(), p_track_path.get_subnames(), false);
	const Resolver *resolver = property_paths.getptr(property_path);
	return resolver ? *resolver : nullptr;
}

void WGodotAnimationProperty::clear() {
	property_paths.clear();
}
