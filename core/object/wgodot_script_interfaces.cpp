// wgodot-changes::file

#include "class_db.h"
#include "script_language.h"

bool Script::wgodot_is_interface_type() const {
	return false;
}

String Script::wgodot_get_interface_id() const {
	return String();
}

bool Script::wgodot_implements_interface(const String &p_interface_id) const {
	return false;
}

void Script::wgodot_get_interface_ids(HashSet<String> &r_interfaces) const {
}

Error Script::wgodot_validate_native_implementation(const StringName &p_class, String &r_error) const {
	r_error = "This script does not define an interface.";
	return ERR_INVALID_PARAMETER;
}

bool Script::wgodot_is_instance_compatible(Object *p_object) const {
	if (p_object == nullptr) {
		return true;
	}
	if (wgodot_is_interface_type()) {
		const String id = wgodot_get_interface_id();
		if (ClassDB::wgodot_class_implements_interface(p_object->get_class_name(), id)) {
			return true;
		}
		Ref<Script> actual = p_object->get_script();
		return actual.is_valid() && actual->wgodot_implements_interface(id);
	}
	Ref<Script> actual = p_object->get_script();
	return actual.is_valid() && actual->inherits_script(Ref<Script>(const_cast<Script *>(this)));
}

bool Script::wgodot_is_type_compatible(const Ref<Script> &p_script) const {
	if (p_script.is_null()) {
		return false;
	}
	if (p_script.ptr() == this) {
		return true;
	}
	if (wgodot_is_interface_type()) {
		const String id = wgodot_get_interface_id();
		return p_script->wgodot_implements_interface(id) || ClassDB::wgodot_class_implements_interface(p_script->get_instance_base_type(), id);
	}
	return p_script->inherits_script(Ref<Script>(const_cast<Script *>(this)));
}

Error ClassDB::wgodot_register_interface(const StringName &p_class, const Script *p_interface) {
	ERR_FAIL_NULL_V(p_interface, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(!p_interface->wgodot_is_interface_type(), ERR_INVALID_PARAMETER);
	String error;
	Error result = p_interface->wgodot_validate_native_implementation(p_class, error);
	ERR_FAIL_COND_V_MSG(result != OK, result, vformat("Class '%s' cannot implement interface '%s': %s", p_class, p_interface->get_global_name(), error));
	Locker::Lock lock(Locker::STATE_WRITE);
	ClassInfo *info = classes.getptr(p_class);
	ERR_FAIL_NULL_V(info, ERR_DOES_NOT_EXIST);
	p_interface->wgodot_get_interface_ids(info->wgodot_interfaces);
	return OK;
}

bool ClassDB::wgodot_class_implements_interface(const StringName &p_class, const String &p_interface_id) {
	Locker::Lock lock(Locker::STATE_READ);
	for (const ClassInfo *info = classes.getptr(p_class); info != nullptr; info = info->inherits_ptr) {
		if (info->wgodot_interfaces.has(p_interface_id)) {
			return true;
		}
	}
	return false;
}

bool ClassDB::wgodot_interface_has_native_implementation(const String &p_interface_id) {
	Locker::Lock lock(Locker::STATE_READ);
	for (const KeyValue<StringName, ClassInfo> &entry : classes) {
		if (entry.value.wgodot_interfaces.has(p_interface_id)) {
			return true;
		}
	}
	return false;
}
