// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/io/resource_uid.h"

// Ordered assignments from a text resource, before resource getters synthesize
// defaults or derived data. Used only when building its disposable binary cache.
struct WGodotResourceProperty {
	StringName name;
	Variant value;
};

using WGodotResourceProperties = HashMap<Ref<Resource>, List<WGodotResourceProperty>>;

// Preserve authored external identities, including references with no UID.
// Importing a dependency can replace its UID without changing the source text.
using WGodotResourceUIDs = HashMap<Ref<Resource>, ResourceUID::ID>;
