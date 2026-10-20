// wgodot-changes::file
#include "class_db.h"

#ifdef TOOLS_ENABLED

void ClassDB::add_native_property_pattern(const StringName &p_class, const NativePropertyPattern &p_pattern) {
	Locker::Lock lock(Locker::STATE_WRITE);
	ClassInfo *info = classes.getptr(p_class);
	ERR_FAIL_NULL(info);
	ERR_FAIL_COND(!get_method(p_class, p_pattern.setter));
	ERR_FAIL_COND(!p_pattern.getter.is_empty() && !get_method(p_class, p_pattern.getter));
	ERR_FAIL_COND(!p_pattern.collection.is_empty() && !get_method(p_class, p_pattern.clear_collection));
	ERR_FAIL_COND(p_pattern.index_type != Variant::INT && p_pattern.index_type != Variant::STRING_NAME);
	info->native_property_patterns.push_back(p_pattern);
}

ClassDB::NativeProperty ClassDB::get_native_property(const StringName &p_class, const StringName &p_property, Variant::Type p_value_type) {
	Locker::Lock lock(Locker::STATE_READ);
	NativeProperty result;
	result.setter = get_method(p_class, get_property_setter(p_class, p_property));
	result.getter = get_method(p_class, get_property_getter(p_class, p_property));
	if (result.setter) {
		const int index = get_property_index(p_class, p_property);
		result.indexed = index >= 0;
		result.index = index;
		return result;
	}
	const String name = p_property;
	for (const ClassInfo *info = classes.getptr(p_class); info; info = info->inherits_ptr) {
		for (const NativePropertyPattern &pattern : info->native_property_patterns) {
			if (!pattern.collection.is_empty() && p_property == pattern.collection) {
				result.setter = get_method(p_class, pattern.clear_collection);
				result.collection_prefix = pattern.prefix;
				return result;
			}
			if (!name.begins_with(pattern.prefix) || !name.ends_with(pattern.suffix) || name.length() < pattern.prefix.length() + pattern.suffix.length() ||
					(pattern.value_type != Variant::NIL && pattern.value_type != p_value_type)) {
				continue;
			}
			const String index = name.substr(pattern.prefix.length(), name.length() - pattern.prefix.length() - pattern.suffix.length());
			if (pattern.index_type == Variant::INT) {
				if (!index.is_valid_int() || index.to_int() < 0) {
					continue;
				}
				result.index = index.to_int();
			} else {
				result.index = StringName(index);
			}
			result.indexed = true;
			result.setter = get_method(p_class, pattern.setter);
			result.getter = pattern.getter.is_empty() ? nullptr : get_method(p_class, pattern.getter);
			return result;
		}
	}
	return result;
}

#endif
