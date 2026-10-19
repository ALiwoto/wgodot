// wgodot-changes::file
#pragma once

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
#include "core/io/resource.h"
#include "scene/resources/animation.h"

// Debug-only measurements using the real text loader. Fresh objects always;
// callers explicitly choose whether the editor's binary disk cache participates.
class WResourceLoadMeasurement : public RefCounted {
	GDCLASS(WResourceLoadMeasurement, RefCounted);

	Ref<Resource> resource;
	uint64_t load_usec = 0;
	Error error = OK;

protected:
	static void _bind_methods();

public:
	static Ref<WResourceLoadMeasurement> read_text(const String &p_path, bool p_disk_cache = false);
	static String compare_animations(const Ref<Animation> &p_expected, const Ref<Animation> &p_actual);
	Ref<Resource> get_resource() const { return resource; }
	uint64_t get_load_usec() const { return load_usec; }
	Error get_error() const { return error; }
	uint64_t release();
};
#endif
