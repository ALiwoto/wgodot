// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/io/resource.h"

class WResourceReload : public RefCounted {
	GDCLASS(WResourceReload, RefCounted);

protected:
	static void _bind_methods();

public:
	// Explicit live-inspection mutation. Callers must hold gameplay/render use idle.
	static Error dangerous_reload_cached(const Ref<Resource> &p_resource);
};
#endif
