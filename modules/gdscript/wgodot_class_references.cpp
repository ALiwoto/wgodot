// wgodot-changes::file
#include "wgodot_class_references.h"

#include "gdscript.h"

#include "core/io/wgodot_named_types.h"
#include "core/object/wgodot_class_references.h"

namespace {
Variant resolve(const StringName &p_name) {
	const String text = p_name;
	const StringName root = text.get_slice("::", 0);
	if (ScriptServer::is_global_class(root)) {
		const Ref<GDScript> script = WGodotNamedTypes::script(root);
		if (script.is_valid()) {
			return Ref<GDScript>(script->find_class(text));
		}
		return Variant();
	}
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	if (const int *index = language->get_global_map().getptr(p_name)) {
		const Variant &value = language->get_global_array()[*index];
		if (value.get_type() == Variant::OBJECT && Object::cast_to<GDScriptNativeClass>(value.get_validated_object())) {
			return value;
		}
	}
	return Variant();
}

StringName name(const Object *p_object) {
	if (const auto *script = Object::cast_to<GDScript>(p_object)) {
		if (!script->wgodot_class_reference) {
			return StringName();
		}
		const String qualified = script->get_fully_qualified_name();
		return ScriptServer::is_global_class(qualified.get_slice("::", 0)) ? StringName(qualified) : StringName();
	}
	if (const auto *native = Object::cast_to<GDScriptNativeClass>(p_object)) {
		return native->get_name();
	}
	return StringName();
}

StringName native_base(const Object *p_object) {
	const auto *native = Object::cast_to<GDScriptNativeClass>(p_object);
	return native ? native->get_name() : StringName();
}
} // namespace

void WGodotGDScriptClassReferences::initialize() {
	WGodotClassReferences::set_language({resolve, name, native_base});
}

void WGodotGDScriptClassReferences::finish() {
	WGodotClassReferences::set_language({});
}
