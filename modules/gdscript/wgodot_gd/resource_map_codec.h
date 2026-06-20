// wgodot-changes::file
/**************************************************************************/
/*  resource_map_codec.h                                                  */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptResourceMapCodec {

Vector<uint8_t> encode_resource_map(const String &p_path, const Vector<uint8_t> &p_raw);
Vector<uint8_t> decode_resource_map(const String &p_path, const Vector<uint8_t> &p_encoded);

} // namespace WGodotGDScriptResourceMapCodec
