// wgodot-changes::file
#pragma once

#include "wgodot_native_packed.h"

#include "core/object/wgodot_interface.h"

class BinarySerializable {
	WGD_INTERFACE(BinarySerializable);

public:
	virtual WGodotNative::Packed<PackedByteArray> serialize_binary() = 0;
};
