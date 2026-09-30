// wgodot-changes::file
#pragma once

#include "wgodot_native_packed.h"

#include "core/object/wgodot_interface.h"

class BinarySerializable {
	WGD_INTERFACE(BinarySerializable);

public:
	virtual WGodotNative::WArray<uint8_t, Variant::PACKED_BYTE_ARRAY> serialize_binary() = 0;
};
