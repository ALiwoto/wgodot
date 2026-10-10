// wgodot-changes::file
#pragma once

#include "core/io/resource.h"

// Ordered assignments from a text resource, before resource getters synthesize
// defaults or derived data. Used only when building its disposable binary cache.
struct WGodotResourceProperty {
	StringName name;
	Variant value;
};

using WGodotResourceProperties = HashMap<Ref<Resource>, List<WGodotResourceProperty>>;
