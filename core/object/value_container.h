// wgodot-changes::file
/**************************************************************************/
/*  value_container.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"

class ValueContainer : public RefCounted {
	GDCLASS(ValueContainer, RefCounted);

	Variant default_value;
	Variant value;

protected:
	static void _bind_methods();

public:
	static Ref<ValueContainer> create(const Variant &p_default_value);

	void set_initial_value(const Variant &p_value);
	Variant get_value() const;
	void change_value(const Variant &p_value);
	void clear_value();

	ValueContainer();
};
