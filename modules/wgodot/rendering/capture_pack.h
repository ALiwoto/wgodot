// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "readback_codec.h"
#include "core/object/ref_counted.h"

class Image;

namespace WCapturePack {
constexpr int BLOCK_BYTES = WReadbackCodec::BLOCK_BYTES;

// Capture writes only raw, self-contained spools. Packing is an explicit CLI
// operation after the entire session stops; it never runs inside the recorder.
Error store_image(const Ref<Image> &p_image, const String &p_path);
int run(const Vector<String> &p_arguments);
}
#endif
