// wgodot-changes::file
#include "element_behavior.h"

#include "scene/main/viewport.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box_texture.h"

ElementBehavior::ElementBehavior(Control *p_control) : control(p_control) {
	if (Label *native_label = Object::cast_to<Label>(control)) {
		native_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		native_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	} else if (!Object::cast_to<BaseButton>(control) && !Object::cast_to<LineEdit>(control)) {
		control->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	}
}

Label *ElementBehavior::get_label() {
	if (Label *native_label = Object::cast_to<Label>(control)) {
		return native_label;
	}
	if (!label) {
		label = memnew(Label);
		label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		control->add_child(label, false, Node::INTERNAL_MODE_BACK);
		label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		if (control->has_theme_font_override("font")) {
			label->add_theme_font_override("font", get_font());
		}
		label->add_theme_font_size_override("font_size", get_font_size());
		label->add_theme_color_override("font_color", fore_color);
		update_label_alignment();
	}
	return label;
}

void ElementBehavior::rect_changed() {
	const Vector2 position = control->get_position();
	const Vector2 size = control->get_size();
	if (previous_position != position) {
		previous_position = position;
		control->emit_signal("position_changed", control, position);
	}
	if (previous_size != size) {
		previous_size = size;
		control->emit_signal("size_changed", control, size);
	}
}

void ElementBehavior::notification(int p_what) {
	switch (p_what) {
		case Control::NOTIFICATION_MOUSE_ENTER:
		case Control::NOTIFICATION_MOUSE_EXIT: {
			hovering = p_what == Control::NOTIFICATION_MOUSE_ENTER;
			control->emit_signal(hovering ? "mouse_enter" : "mouse_leave", control, control->get_global_transform_with_canvas().xform(control->get_local_mouse_position()));
			if (hover_effect && control->is_inside_tree()) {
				if (hover_tween.is_valid()) {
					hover_tween->kill();
				}
				control->set_pivot_offset(control->get_size() * 0.5);
				hover_tween = control->create_tween();
				hover_tween->tween_property(control, NodePath("scale"), hovering && is_enabled() ? Vector2(1.06, 1.06) : Vector2(1, 1), 0.12);
			}
		} break;
		case CanvasItem::NOTIFICATION_DRAW: {
			// Label, Button and LineEdit draw their own themed background and text.
			if (Object::cast_to<Label>(control) || Object::cast_to<BaseButton>(control) || Object::cast_to<LineEdit>(control)) {
				break;
			}
			const Rect2 rect(bg_position, control->get_size());
			if (back_color.a > 0) {
				control->draw_rect(rect, back_color);
			}
			if (background.is_valid()) {
				control->draw_texture_rect(background, rect, false);
			}
			if (image.is_valid()) {
				control->draw_texture_rect(image, rect, false, image_modulate);
			}
		} break;
		case Node::NOTIFICATION_EXIT_TREE:
		case Node::NOTIFICATION_WM_WINDOW_FOCUS_OUT: {
			pointer_index = -2;
			dragging = false;
		} break;
	}
}

void ElementBehavior::update_input() {
	const bool native_enabled = Object::cast_to<BaseButton>(control) || Object::cast_to<LineEdit>(control);
	const bool blocked = input_disabled || (!native_enabled && !enabled);
	control->set_mouse_behavior_recursive(blocked ? Control::MOUSE_BEHAVIOR_DISABLED : Control::MOUSE_BEHAVIOR_INHERITED);
	control->set_focus_behavior_recursive(blocked ? Control::FOCUS_BEHAVIOR_DISABLED : Control::FOCUS_BEHAVIOR_INHERITED);
	if (blocked) {
		pointer_index = -2;
		dragging = false;
	}
}

void ElementBehavior::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (BaseButton *button = Object::cast_to<BaseButton>(control)) {
		button->set_disabled(!p_enabled);
	} else if (LineEdit *edit = Object::cast_to<LineEdit>(control)) {
		edit->set_editable(p_enabled);
	}
	update_input();
}

bool ElementBehavior::is_enabled() const {
	if (const BaseButton *button = Object::cast_to<BaseButton>(control)) {
		return !button->is_disabled();
	}
	if (const LineEdit *edit = Object::cast_to<LineEdit>(control)) {
		return edit->is_editable();
	}
	return enabled;
}

void ElementBehavior::set_input_disabled(bool p_disabled) {
	input_disabled = p_disabled;
	if (!p_disabled && control->get_mouse_filter() == Control::MOUSE_FILTER_IGNORE) {
		control->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	}
	update_input();
}

void ElementBehavior::set_movements(int p_movements) {
	ERR_FAIL_COND(p_movements < 0 || p_movements > 3);
	movements = p_movements;
	if (movements != 0) {
		control->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	}
}

void ElementBehavior::set_hover_effect(bool p_enabled) {
	hover_effect = p_enabled;
	if (!hover_effect) {
		if (hover_tween.is_valid()) {
			hover_tween->kill();
		}
		control->set_scale(Vector2(1, 1));
	}
}

void ElementBehavior::gui_input(const Ref<InputEvent> &p_event) {
	if (!is_enabled() || input_disabled) {
		return;
	}
	Ref<InputEventMouse> mouse = p_event;
	Ref<InputEventMouseButton> button = p_event;
	Ref<InputEventMouseMotion> motion = p_event;
	Ref<InputEventScreenTouch> touch = p_event;
	Ref<InputEventScreenDrag> drag = p_event;
	if (mouse.is_valid() && mouse->get_device() == InputEvent::DEVICE_ID_EMULATION) {
		return;
	}
	Vector2 local;
	int index = -1;
	bool primary = false;
	bool pressed = false;
	if (mouse.is_valid()) {
		local = mouse->get_position();
		primary = button.is_valid() && button->get_button_index() == MouseButton::LEFT;
		pressed = primary && button->is_pressed();
	} else if (touch.is_valid()) {
		local = touch->get_position();
		index = touch->get_index();
		primary = true;
		pressed = touch->is_pressed();
	} else if (drag.is_valid()) {
		local = drag->get_position();
		index = drag->get_index();
	} else {
		pointer_position = control->get_global_transform_with_canvas().xform(control->get_size() * 0.5);
		return;
	}
	pointer_position = control->get_global_transform_with_canvas().xform(local);
	if (primary && pressed && pointer_index == -2) {
		pointer_index = index;
		drag_origin = pointer_position;
		control_origin = control->get_position();
		control->emit_signal("left_down", control, pointer_position);
	} else if (primary && !pressed && pointer_index == index) {
		pointer_index = -2;
		dragging = false;
		control->emit_signal("left_up", control, pointer_position);
	} else if ((motion.is_valid() || drag.is_valid()) && pointer_index == index && movements != 0) {
		Vector2 offset = pointer_position - drag_origin;
		const CanvasItem *parent = control->get_parent_item();
		if (parent) {
			offset = parent->get_global_transform_with_canvas().basis_xform_inv(offset);
		}
		if (!(movements & 2)) {
			offset.x = 0;
		}
		if (!(movements & 1)) {
			offset.y = 0;
		}
		if (!dragging && offset.length() >= 8) {
			dragging = true;
			control->propagate_notification(Control::NOTIFICATION_SCROLL_BEGIN);
		}
		if (dragging) {
			control->set_position(control_origin + offset);
			control->accept_event();
		}
	}
}

void ElementBehavior::pressed() {
	control->emit_signal("left_click", control, pointer_position);
	control->emit_signal("clicked", control, pointer_position);
}

void ElementBehavior::change_parent(Node *p_parent) {
	// Older procedural builders supply the parent before adding the child.
	pending_parent = p_parent ? p_parent->get_instance_id() : ObjectID();
}

Node *ElementBehavior::get_element_parent() const {
	return control->get_parent() ? control->get_parent() : Object::cast_to<Node>(ObjectDB::get_instance(pending_parent));
}

void ElementBehavior::center_to_screen() {
	control->set_position((control->get_viewport_rect().size - control->get_size()) * 0.5);
}

void ElementBehavior::change_position_mid_width(float p_y) {
	const Control *parent = Object::cast_to<Control>(get_element_parent());
	const float width = parent ? parent->get_size().x : control->get_viewport_rect().size.x;
	control->set_position(Vector2((width - control->get_size().x) * 0.5, p_y));
}

void ElementBehavior::set_text(const String &p_text) {
	if (Button *button = Object::cast_to<Button>(control)) {
		button->set_text(p_text);
	} else if (LineEdit *edit = Object::cast_to<LineEdit>(control)) {
		edit->set_text(p_text);
	} else {
		get_label()->set_text(p_text);
	}
}

String ElementBehavior::get_text() const {
	if (const Button *button = Object::cast_to<Button>(control)) {
		return button->get_text();
	}
	if (const LineEdit *edit = Object::cast_to<LineEdit>(control)) {
		return edit->get_text();
	}
	if (const Label *native_label = Object::cast_to<Label>(control)) {
		return native_label->get_text();
	}
	return label ? label->get_text() : String();
}

void ElementBehavior::translate_text(const String &p_key, const TypedArray<String> &p_arguments) {
	String text = control->tr(p_key);
	if (!p_arguments.is_empty()) {
		bool error = false;
		text = text.sprintf(p_arguments, &error);
		ERR_FAIL_COND_MSG(error, text);
	}
	set_text(text);
}

void ElementBehavior::set_font(const Ref<Font> &p_font) {
	if (p_font.is_valid()) {
		control->add_theme_font_override("font", p_font);
		if (label) {
			label->add_theme_font_override("font", p_font);
		}
	} else {
		control->remove_theme_font_override("font");
		if (label) {
			label->remove_theme_font_override("font");
		}
	}
}

Ref<Font> ElementBehavior::get_font() const {
	return control->has_theme_font_override("font") ? control->get_theme_font("font") : Ref<Font>();
}

void ElementBehavior::set_font_size(int p_size) {
	control->add_theme_font_size_override("font_size", p_size);
	if (label) {
		label->add_theme_font_size_override("font_size", p_size);
	}
}

int ElementBehavior::get_font_size() const {
	return control->get_theme_font_size("font_size");
}

void ElementBehavior::update_label_alignment() {
	int alignment = text_alignment;
	if (alignment == 0) {
		return;
	}
	// Preserve the public enum's historical gap at value 6.
	if (alignment >= 7) {
		alignment--;
	}
	const HorizontalAlignment horizontal = HorizontalAlignment((alignment - 1) % 3);
	const VerticalAlignment vertical = VerticalAlignment((alignment - 1) / 3);
	if (Button *button = Object::cast_to<Button>(control)) {
		button->set_text_alignment(horizontal);
	} else if (LineEdit *edit = Object::cast_to<LineEdit>(control)) {
		edit->set_horizontal_alignment(horizontal);
	} else if (Label *native_label = Object::cast_to<Label>(control)) {
		native_label->set_horizontal_alignment(horizontal);
		native_label->set_vertical_alignment(vertical);
	} else if (label) {
		label->set_horizontal_alignment(horizontal);
		label->set_vertical_alignment(vertical);
	}
}

void ElementBehavior::set_text_alignment(int p_alignment) {
	ERR_FAIL_COND(p_alignment < 0 || p_alignment > 10 || p_alignment == 6);
	text_alignment = p_alignment;
	update_label_alignment();
}

void ElementBehavior::set_outline_size(int p_size) {
	control->add_theme_constant_override("outline_size", p_size);
	if (label) {
		label->add_theme_constant_override("outline_size", p_size);
	}
}

void ElementBehavior::set_back_color(const Color &p_color) {
	back_color = p_color;
	if (Object::cast_to<Label>(control) || Object::cast_to<LineEdit>(control)) {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(p_color);
		control->add_theme_style_override("normal", style);
	}
	control->queue_redraw();
}

void ElementBehavior::set_fore_color(const Color &p_color) {
	fore_color = p_color;
	control->add_theme_color_override("font_color", p_color);
	if (label) {
		label->add_theme_color_override("font_color", p_color);
	}
}

void ElementBehavior::set_image(const Ref<Texture2D> &p_image) {
	image = p_image;
	if (Button *button = Object::cast_to<Button>(control)) {
		button->set_button_icon(p_image);
	}
	control->queue_redraw();
}

void ElementBehavior::set_background(const Ref<Texture2D> &p_image) {
	background = p_image;
	if (Object::cast_to<Label>(control)) {
		if (p_image.is_valid()) {
			Ref<StyleBoxTexture> style;
			style.instantiate();
			style->set_texture(p_image);
			control->add_theme_style_override("normal", style);
		} else {
			control->remove_theme_style_override("normal");
		}
	}
	control->queue_redraw();
}

void ElementBehavior::set_image_modulate(const Color &p_color) {
	image_modulate = p_color;
	control->queue_redraw();
}

void ElementBehavior::set_bg_position(const Vector2 &p_position) {
	bg_position = p_position;
	control->queue_redraw();
}

void ElementBehavior::move_to(const Vector2 &p_position, double p_duration, double p_delay, const Callable &p_finished) {
	if (moving_tween.is_valid()) {
		moving_tween->kill();
	}
	moving_tween = control->create_tween();
	Ref<PropertyTweener> property = moving_tween->tween_property(control, NodePath("position"), p_position, p_duration);
	property->set_ease(Tween::EASE_OUT);
	property->set_trans(Tween::TRANS_QUINT);
	property->set_delay(p_delay);
	moving_tween->connect("finished", p_finished, Object::CONNECT_ONE_SHOT);
}

void ElementBehavior::fade_to(double p_alpha, double p_duration, double p_delay, const Callable &p_finished) {
	if (fading_tween.is_valid()) {
		fading_tween->kill();
	}
	fading_tween = control->create_tween();
	Ref<PropertyTweener> property = fading_tween->tween_property(control, NodePath("modulate:a"), p_alpha, p_duration);
	property->set_delay(p_delay);
	fading_tween->connect("finished", p_finished, Object::CONNECT_ONE_SHOT);
}

void ElementBehavior::animation_finished(bool p_moving) {
	control->emit_signal(p_moving ? "on_element_moving_finished" : "on_element_fading_finished", control);
}

bool ElementBehavior::is_moving() const {
	return moving_tween.is_valid() && moving_tween->is_running();
}

bool ElementBehavior::is_fading() const {
	return fading_tween.is_valid() && fading_tween->is_running();
}

void ElementBehavior::stop_moving() {
	if (is_moving()) {
		moving_tween->kill();
		control->emit_signal("on_element_moving_stopped", control);
	}
}

void ElementBehavior::stop_fading() {
	if (is_fading()) {
		fading_tween->kill();
		control->emit_signal("on_element_fading_stopped", control);
	}
}
