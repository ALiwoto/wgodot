// wgodot-changes::file
#include "wgodot_native_resource_fields.h"

#include "core/object/class_db.h"
#include "core/os/rw_lock.h"

namespace {
RWLock schema_lock;
HashMap<StringName, WGodotNativeResourceFields::Resolver> schemas;
}

void WGodotNativeResourceFields::register_type(const StringName &p_class, Resolver p_resolver) {
	RWLockWrite lock(schema_lock);
	ERR_FAIL_COND(schemas.has(p_class));
	schemas.insert(p_class, p_resolver);
}

void WGodotNativeResourceFields::unregister_type(const StringName &p_class) {
	RWLockWrite lock(schema_lock);
	schemas.erase(p_class);
}

bool WGodotNativeResourceFields::field(const StringName &p_class, const String &p_name, PropertyInfo &r_info) {
	for (StringName type = p_class; !type.is_empty(); type = ClassDB::get_parent_class(type)) {
		Resolver resolver = nullptr;
		{
			RWLockRead lock(schema_lock);
			if (const Resolver *entry = schemas.getptr(type)) {
				resolver = *entry;
			}
		}
		if (resolver && resolver(p_name, r_info)) {
			return true;
		}
	}
	return false;
}
