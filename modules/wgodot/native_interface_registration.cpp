// wgodot-changes::file
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_GDSCRIPT_ENABLED
#include "native/binary_serializable.h"

#include "core/object/wgodot_interface_registry.h"
void BinarySerializable::_bind_interface(WGodotNativeInterfaces::Builder &p_builder) {
	p_builder.method("serialize_binary", &BinarySerializable::serialize_binary);
}
#endif
