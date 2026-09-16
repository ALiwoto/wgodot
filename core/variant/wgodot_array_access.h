// wgodot-changes::file
#pragma once

class Array;

namespace WGodotNative {
// Inspect ownership before the native iterator acquires its Array handle.
bool array_is_shared(const Array &p_array);
} //namespace WGodotNative
