// wgodot-changes::file
#include "core/io/resource.h"
#include "core/object/script_language.h"

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

Ref<Resource> Resource::_duplicate_resource(const Ref<Resource> &p_resource, const DuplicateParams &p_params, uint32_t p_usage) const {
	if (p_resource.is_null()) {
		return p_resource;
	}
	bool should_duplicate = false;
	if (p_usage & PROPERTY_USAGE_ALWAYS_DUPLICATE) {
		should_duplicate = true;
	} else if (p_usage & PROPERTY_USAGE_NEVER_DUPLICATE) {
		should_duplicate = false;
	} else if (p_params.local_scene) {
		should_duplicate = p_resource->is_local_to_scene();
	} else {
		switch (p_params.subres_mode) {
			case RESOURCE_DEEP_DUPLICATE_NONE:
				break;
			case RESOURCE_DEEP_DUPLICATE_INTERNAL:
				should_duplicate = p_params.deep && p_resource->is_built_in();
				break;
			case RESOURCE_DEEP_DUPLICATE_ALL:
				should_duplicate = p_params.deep;
				break;
			default:
				DEV_ASSERT(false);
		}
		if (Object::cast_to<Script>(p_resource.ptr())) {
			should_duplicate = false;
		}
	}
	if (!should_duplicate) {
		return p_resource;
	}
	if (const Ref<Resource> *existing = thread_duplicate_remap_cache->getptr(p_resource)) {
		return *existing;
	}
	const Ref<Resource> copy = p_params.local_scene
			? p_resource->duplicate_for_local_scene(p_params.local_scene, *thread_duplicate_remap_cache)
			: p_resource->_duplicate(p_params);
	thread_duplicate_remap_cache->insert(p_resource, copy);
	return copy;
}

Ref<Resource> Resource::NativeCopyContext::copy_resource(const Ref<Resource> &p_resource) const {
	if (params.copy_only) {
		return p_resource;
	}
	DuplicateRemapCacheT *previous_cache = thread_duplicate_remap_cache;
	const bool previous_allocation = thread_duplicate_remap_cache_needs_deallocation;
	thread_duplicate_remap_cache = params.native_cache;
	thread_duplicate_remap_cache_needs_deallocation = false;
	const Ref<Resource> result = owner._duplicate_resource(p_resource, params, 0);
	thread_duplicate_remap_cache = previous_cache;
	thread_duplicate_remap_cache_needs_deallocation = previous_allocation;
	return result;
}
