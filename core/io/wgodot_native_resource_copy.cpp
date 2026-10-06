// wgodot-changes::file
#include "core/io/resource.h"

void Resource::_wgodot_copy_local_scene(Resource *p_copy, Node *p_scene, DuplicateRemapCacheT &p_cache) const {
	DuplicateRemapCacheT *previous_cache = thread_duplicate_remap_cache;
	const bool previous_allocation = thread_duplicate_remap_cache_needs_deallocation;
	thread_duplicate_remap_cache = nullptr;
	DuplicateParams params;
	params.deep = true;
	params.local_scene = p_scene;
	params.native_cache = &p_cache;
	_wgodot_native_copy(p_copy, params);
	thread_duplicate_remap_cache = previous_cache;
	thread_duplicate_remap_cache_needs_deallocation = previous_allocation;
}

Variant Resource::_wgodot_duplicate_value(const Variant &p_value, const DuplicateParams &p_params, uint32_t p_usage) const {
	DuplicateRemapCacheT *previous_cache = thread_duplicate_remap_cache;
	const bool previous_allocation = thread_duplicate_remap_cache_needs_deallocation;
	thread_duplicate_remap_cache = p_params.native_cache;
	thread_duplicate_remap_cache_needs_deallocation = false;
	const Variant result = _duplicate_recursive(p_value, p_params, p_usage);
	thread_duplicate_remap_cache = previous_cache;
	thread_duplicate_remap_cache_needs_deallocation = previous_allocation;
	return result;
}
