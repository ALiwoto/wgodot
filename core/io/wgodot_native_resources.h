// wgodot-changes::file
#pragma once

#include "core/io/resource_loader.h"

namespace WGodotNativeResources {
struct Definition {
	int64_t id;
	const char *type;
	Ref<Resource> (*create)(ResourceFormatLoader::CacheMode);
};

void initialize(const Definition *p_definitions, int p_count);
void clear();
} // namespace WGodotNativeResources
