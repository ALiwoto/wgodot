// wgodot-changes::file
#include "wgodot_class_references.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/mutex.h"

namespace {
WGodotClassReferences::Language language;
Mutex native_mutex;
HashMap<StringName, Ref<WNativeClassReference>> native_classes;
constexpr const char *CONSTRAINT_PREFIX = "ClassRef[";

Variant instantiate_native(const StringName &p_name) {
	Object *instance = ClassDB::instantiate_no_placeholders(p_name);
	if (RefCounted *reference = Object::cast_to<RefCounted>(instance)) {
		return Ref<RefCounted>(reference);
	}
	return instance;
}
} // namespace

void WNativeClassReference::initialize(const StringName &p_name, Constructor p_constructor) {
	name = p_name;
	constructor = p_constructor;
}

Variant WNativeClassReference::instantiate() const {
	ERR_FAIL_NULL_V_MSG(constructor, Variant(), "Class '" + String(name) + "' cannot be constructed without arguments.");
	return constructor(name);
}

void WGodotClassReferences::set_language(const Language &p_language) {
	language = p_language;
}

void WGodotClassReferences::register_native(const StringName &p_name, WNativeClassReference::Constructor p_constructor) {
	Ref<WNativeClassReference> reference;
	reference.instantiate();
	reference->initialize(p_name, p_constructor);
	MutexLock lock(native_mutex);
	native_classes.insert(p_name, reference);
}

void WGodotClassReferences::clear() {
	MutexLock lock(native_mutex);
	native_classes.clear();
}

Variant WGodotClassReferences::resolve(const StringName &p_name) {
	if (p_name == StringName()) {
		return Variant();
	}
	if (language.resolve) {
		Variant result = language.resolve(p_name);
		if (result.get_type() == Variant::OBJECT && result.get_validated_object()) {
			return result;
		}
	}
	{
		MutexLock lock(native_mutex);
		if (const Ref<WNativeClassReference> *reference = native_classes.getptr(p_name)) {
			return *reference;
		}
	}
	if (!ClassDB::class_exists(p_name)) {
		return Variant();
	}
	Ref<WNativeClassReference> reference;
	reference.instantiate();
	reference->initialize(p_name, ClassDB::can_instantiate(p_name) ? instantiate_native : nullptr);
	MutexLock lock(native_mutex);
	// Another loader may have resolved the same native class concurrently.
	if (const Ref<WNativeClassReference> *existing = native_classes.getptr(p_name)) {
		return *existing;
	}
	native_classes.insert(p_name, reference);
	return reference;
}

StringName WGodotClassReferences::name(const Variant &p_reference) {
	if (p_reference.get_type() != Variant::OBJECT) {
		return StringName();
	}
	Object *object = p_reference.get_validated_object();
	if (const auto *native = Object::cast_to<WNativeClassReference>(object)) {
		return native->get_name();
	}
	return object && language.name ? language.name(object) : StringName();
}

StringName WGodotClassReferences::native_base(const Variant &p_reference) {
	if (p_reference.get_type() != Variant::OBJECT) {
		return StringName();
	}
	Object *object = p_reference.get_validated_object();
	if (const auto *native = Object::cast_to<WNativeClassReference>(object)) {
		return native->get_name();
	}
	if (const auto *script = Object::cast_to<Script>(object)) {
		return script->get_instance_base_type();
	}
	return object && language.native_base ? language.native_base(object) : StringName();
}

bool WGodotClassReferences::accepts(const Variant &p_reference, const Variant &p_base) {
	if (name(p_base) == StringName()) {
		return false;
	}
	if (p_reference.get_type() == Variant::NIL || (p_reference.get_type() == Variant::OBJECT && !p_reference.get_validated_object())) {
		return true;
	}
	if (p_reference.get_type() != Variant::OBJECT || name(p_reference) == StringName()) {
		return false;
	}
	const Ref<Script> base_script = p_base;
	if (base_script.is_valid()) {
		const Ref<Script> script = p_reference;
		return script.is_valid() && (script == base_script || script->inherits_script(base_script));
	}
	const StringName base = native_base(p_base);
	const StringName source = native_base(p_reference);
	return base != StringName() && source != StringName() && ClassDB::is_parent_class(source, base);
}

bool WGodotClassReferences::accepts(const Variant &p_reference, const StringName &p_base) {
	return accepts(p_reference, resolve(p_base));
}

StringName WGodotClassReferences::constraint(const StringName &p_base) {
	return String(CONSTRAINT_PREFIX) + String(p_base) + "]";
}

StringName WGodotClassReferences::constraint_base(const StringName &p_constraint) {
	const String text = p_constraint;
	if (!text.begins_with(CONSTRAINT_PREFIX) || !text.ends_with("]")) {
		return StringName();
	}
	return text.substr(9, text.length() - 10);
}
