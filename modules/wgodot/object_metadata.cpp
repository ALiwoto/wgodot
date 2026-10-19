// wgodot-changes::file

#include "object_metadata.h"

#include "core/object/class_db.h"
#include "core/variant/dictionary.h"

Variant ObjectMetadata::get_value(const Object *p_object, const NodePath &p_path, const Variant &p_default) {
	ERR_FAIL_NULL_V(p_object, p_default);
	const Vector<StringName> names = p_path.get_as_property_path().get_subnames();
	ERR_FAIL_COND_V(names.is_empty(), p_default);
	if (!p_object->has_meta(names[0])) {
		return p_default;
	}
	Variant value = p_object->get_meta(names[0]);
	for (int i = 1; i < names.size(); i++) {
		if (value.get_type() != Variant::DICTIONARY) {
			return p_default;
		}
		bool valid = false;
		value = value.get_named(names[i], valid);
		if (!valid) {
			return p_default;
		}
	}
	return value;
}

void ObjectMetadata::_bind_methods() {
	ClassDB::bind_static_method("ObjectMetadata", D_METHOD("get_value", "object", "path", "default"), &ObjectMetadata::get_value, DEFVAL(Variant()));
}
