// wgodot-changes::file
#pragma once

#include "core/io/resource_loader.h"

// Disposable binary payloads for self-contained native text resources. Keep
// scripts and external dependencies on their normal loading/compilation path.
class WGodotTextResourceCache {
	String source_path;
	String source_hash;
	String cache_path;

public:
	explicit WGodotTextResourceCache(const String &p_source_path);
	Ref<Resource> load(const String &p_original_path, bool p_use_sub_threads, float *r_progress, ResourceFormatLoader::CacheMode p_cache_mode) const;
	void store(const Ref<Resource> &p_resource) const;
};
