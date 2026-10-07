// wgodot-changes::file
#pragma once

#include "core/io/resource.h"

namespace WGodotNativeResourcePolicy {
// Imported assets and plain data resources retain their packaged payloads.
// Factories own authored scenes and resources needing native script bindings.
bool is_authored(const String &p_path);
bool needs_factory(const Ref<Resource> &p_resource);
Error validate_external(const Ref<Resource> &p_resource, String &r_error);
} // namespace WGodotNativeResourcePolicy
