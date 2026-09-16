// wgodot-changes::file
#pragma once

#include "core/object/object_id.h"

namespace WGodotNativeLifetime {
// Installed only by a generated native game. Keeps Object independent of the
// generated signal implementation and its concrete payload types.
inline void (*object_deleted)(ObjectID) = nullptr;
} // namespace WGodotNativeLifetime
