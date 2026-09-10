// wgodot-changes::file

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptExportTransform {
class ExportContext;
}

namespace WGodotGDScriptBuiltinClassAliases {
Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context);
}
namespace WGodotGDScriptInterfaceMethodAliases {
Vector<uint8_t> serialize_alias_map(const WGodotGDScriptExportTransform::ExportContext &p_context);
}
namespace WGodotGDScriptStringObfuscation {
Vector<uint8_t> serialize_string_map(const WGodotGDScriptExportTransform::ExportContext &p_context);
}
namespace WGodotGDScriptResourceMapCodec {
Vector<uint8_t> encode_resource_map(const String &p_path, const Vector<uint8_t> &p_raw);
}
