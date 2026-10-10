// wgodot-changes::file
#pragma once

#include "core/io/resource_loader.h"
#include "core/io/wgodot_resource_properties.h"

// Disposable binary payloads for native text resources and PackedScene states.
// Scene dependencies remain external references and follow normal resource loading.
class WGodotTextResourceCache {
	String source_path;
	String source_hash;
	String cache_path;

public:
	explicit WGodotTextResourceCache(const String &p_source_path);
	bool is_enabled() const { return !cache_path.is_empty(); }
	Ref<Resource> load(const String &p_original_path, bool p_use_sub_threads, float *r_progress, ResourceFormatLoader::CacheMode p_cache_mode) const;
	void store(const Ref<Resource> &p_resource, const WGodotResourceProperties &p_source_properties) const;
};
