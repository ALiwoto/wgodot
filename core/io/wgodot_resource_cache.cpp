// wgodot-changes::file

#include "resource.h"

Ref<Resource> ResourceCache::get_or_add(const String &p_path, const Ref<Resource> &p_resource) {
	// Dependency-cycle recovery can run the same load on more than one thread.
	// Keep lookup and registration atomic, including the virtual set_path hook.
	MutexLock cache_lock(lock);
	Ref<Resource> existing = get_ref(p_path);
	if (existing.is_valid()) {
		return existing;
	}

	p_resource->set_path(p_path);
	return p_resource;
}
