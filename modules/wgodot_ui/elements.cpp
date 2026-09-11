// wgodot-changes::file
#include "elements.h"

int ButtonElement::get_current_button_state() const {
	if (is_disabled()) {
		return 2;
	}
	return get_draw_mode() == DRAW_PRESSED || get_draw_mode() == DRAW_HOVER_PRESSED ? 1 : 0;
}

void ButtonElement::_bind_methods() {
	bind_element_methods<ButtonElement>();
	ClassDB::bind_method(D_METHOD("get_current_button_state"), &ButtonElement::get_current_button_state);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_button_state", PROPERTY_HINT_ENUM, "Normal,Clicked,Disabled"), "", "get_current_button_state");
	ClassDB::bind_integer_constant("ButtonElement", "ButtonElementState", "Normal", 0);
	ClassDB::bind_integer_constant("ButtonElement", "ButtonElementState", "Clicked", 1);
	ClassDB::bind_integer_constant("ButtonElement", "ButtonElementState", "Disabled", 2);
}

ButtonElement::ButtonElement() {
	set_clip_text(true);
	set_expand_icon(true);
	set_mouse_filter(MOUSE_FILTER_PASS);
}
