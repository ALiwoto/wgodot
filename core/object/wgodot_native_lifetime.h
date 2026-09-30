// wgodot-changes::file
#pragma once

#include "core/object/object_id.h"

#ifdef DEBUG_ENABLED
class Object;
class String;
#endif

namespace WGodotNativeLifetime {
// Installed only by a generated native game. Keeps Object independent of the
// generated signal implementation and its concrete payload types.
inline void (*object_deleted)(ObjectID) = nullptr;
#ifdef DEBUG_ENABLED
inline void (*object_initialized)(Object *) = nullptr;
inline void (*object_predelete)(ObjectID) = nullptr;
inline void (*object_removed)(ObjectID) = nullptr;
inline String (*describe_object)(ObjectID) = nullptr;
#endif
} // namespace WGodotNativeLifetime
