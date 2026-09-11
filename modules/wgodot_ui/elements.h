// wgodot-changes::file
#pragma once

#include "element_control.h"

class FlatElement : public ElementControl<Label> {
	GDCLASS(FlatElement, Label);

protected:
	void _notification(int p_what) { element_notification(p_what); }
	static void _bind_methods() { bind_element_methods<FlatElement>(); }
};

class SurfaceElement : public ElementControl<Control> {
	GDCLASS(SurfaceElement, Control);

protected:
	void _notification(int p_what) { element_notification(p_what); }
	static void _bind_methods() { bind_element_methods<SurfaceElement>(); }
};

class ButtonElement : public ElementControl<Button> {
	GDCLASS(ButtonElement, Button);

protected:
	void _notification(int p_what) { element_notification(p_what); }
	static void _bind_methods();

public:
	int get_current_button_state() const;
	ButtonElement();
};

class TextBoxElement : public ElementControl<LineEdit> {
	GDCLASS(TextBoxElement, LineEdit);

protected:
	void _notification(int p_what) { element_notification(p_what); }
	static void _bind_methods() { bind_element_methods<TextBoxElement>(); }
};
