// wgodot-changes::file

#pragma once

#include "core/object/object.h"

// Access nested import metadata without exposing a dynamically typed container
// or using Object's reflected member access in otherwise statically typed code.
class ObjectMetadata : public Object {
	GDCLASS(ObjectMetadata, Object);

protected:
	static void _bind_methods();

public:
	static Variant get_value(const Object *p_object, const NodePath &p_path, const Variant &p_default = Variant());
};
