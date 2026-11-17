// wgodot-changes::file
#include "resource_reload.h"

#ifdef TOOLS_ENABLED
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

void WResourceReload::_bind_methods() {
	ClassDB::bind_static_method("WResourceReload", D_METHOD("dangerous_reload_cached", "resource"), &WResourceReload::dangerous_reload_cached);
}

Error WResourceReload::dangerous_reload_cached(const Ref<Resource> &p_resource) {
	ERR_FAIL_COND_V_MSG(!Thread::is_main_thread(), ERR_INVALID_PARAMETER, "Live resource reload must run on the main thread.");
	ERR_FAIL_COND_V(p_resource.is_null(), ERR_INVALID_PARAMETER);
	const String path = p_resource->get_path();
	ERR_FAIL_COND_V_MSG(path.is_empty() || ResourceCache::get_ref(path) != p_resource, ERR_INVALID_PARAMETER, "Live reload requires the published cached resource.");
	Error error = OK;
	const Ref<Resource> loaded = ResourceLoader::load(path, String(), ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP, &error);
	if (error != OK) {
		return error;
	}
	ERR_FAIL_COND_V_MSG(loaded != p_resource, ERR_INVALID_DATA, "Live reload changed resource type; existing references were not replaced.");
	return OK;
}
#endif
