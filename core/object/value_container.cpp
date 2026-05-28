// wgodot-changes::file
/**************************************************************************/
/*  value_container.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "value_container.h"

#include "core/object/class_db.h"

void ValueContainer::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "default_value"), &ValueContainer::create);

	ClassDB::bind_method(D_METHOD("get_value"), &ValueContainer::get_value);
	ClassDB::bind_method(D_METHOD("change_value", "new_value"), &ValueContainer::change_value);
	ClassDB::bind_method(D_METHOD("clear_value"), &ValueContainer::clear_value);

	ADD_SIGNAL(MethodInfo("value_changed",
			PropertyInfo(Variant::OBJECT, "sender", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, ValueContainer::get_class_static()),
			PropertyInfo(Variant::NIL, "old_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT),
			PropertyInfo(Variant::NIL, "new_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT)));
}

Ref<ValueContainer> ValueContainer::create(const Variant &p_default_value) {
	Ref<ValueContainer> value_container;
	value_container.instantiate();
	value_container->set_initial_value(p_default_value);
	return value_container;
}

void ValueContainer::set_initial_value(const Variant &p_value) {
	default_value = p_value;
	value = p_value;
}

Variant ValueContainer::get_value() const {
	return value;
}

void ValueContainer::change_value(const Variant &p_value) {
	Variant old_value = value;
	value = p_value;
	emit_signal(SNAME("value_changed"), this, old_value, value);
}

void ValueContainer::clear_value() {
	change_value(default_value);
}

ValueContainer::ValueContainer() {}
