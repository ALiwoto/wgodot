// wgodot-changes::file
#include "gdscript.h"

Variant *GDScriptInstance::wgodot_packed_initialization_slot(const StringName &p_name) {
	const auto *member = script->member_indices.getptr(p_name);
	if (!script->valid || !member || member->getter || member->setter ||
			member->data_type.kind != GDScriptDataType::BUILTIN ||
			member->data_type.builtin_type < Variant::PACKED_BYTE_ARRAY ||
			member->data_type.builtin_type > Variant::PACKED_VECTOR4_ARRAY) {
		return nullptr;
	}
	return &members[member->index];
}
