// wgodot-changes::file
#include "smooth_scroll_element.h"

#include "core/os/os.h"

Vector2 SmoothScrollElement::get_scroll_limit() const {
	// The bars remain authoritative for layout, page size and valid positions.
	SmoothScrollElement *self = const_cast<SmoothScrollElement *>(this);
	return Vector2(MAX(0.0, self->get_h_scroll_bar()->get_max() - self->get_h_scroll_bar()->get_page()), MAX(0.0, self->get_v_scroll_bar()->get_max() - self->get_v_scroll_bar()->get_page()));
}

bool SmoothScrollElement::can_scroll_axis(int p_axis) const {
	if (p_axis == 0) {
		return get_horizontal_scroll_mode() != SCROLL_MODE_DISABLED && (get_scroll_limit().x > 0 || small_horizontal);
	}
	return get_vertical_scroll_mode() != SCROLL_MODE_DISABLED && (get_scroll_limit().y > 0 || small_vertical);
}

Vector2 SmoothScrollElement::_wgodot_get_scroll_displacement() const {
	return Vector2(get_h_scroll(), get_v_scroll()) - scroll_position;
}

void SmoothScrollElement::apply_scroll_position(const Vector2 &p_position) {
	scroll_position = p_position;
	updating_bars = true;
	get_h_scroll_bar()->set_value(p_position.x);
	get_v_scroll_bar()->set_value(p_position.y);
	updating_bars = false;
	queue_sort();
}

void SmoothScrollElement::bar_changed(double p_value, int p_axis) {
	if (!updating_bars) {
		scroll_position[p_axis] = p_value;
		velocity[p_axis] = 0;
	}
}

void SmoothScrollElement::begin_scrolling() {
	if (!scrolling) {
		scrolling = true;
		notifying_scroll = true;
		propagate_notification(NOTIFICATION_SCROLL_BEGIN);
		notifying_scroll = false;
		emit_signal("scroll_started");
	}
	set_process_internal(true);
}

void SmoothScrollElement::stop_scrolling() {
	pointer_index = -2;
	active_axis = -1;
	velocity = Vector2();
	set_process_internal(false);
	if (scrolling) {
		scrolling = false;
		propagate_notification(NOTIFICATION_SCROLL_END);
		emit_signal("scroll_ended");
	}
}

void SmoothScrollElement::process_motion(double p_delta) {
	if (pointer_index != -2) {
		return;
	}
	const Vector2 limit = get_scroll_limit();
	Vector2 position = scroll_position;
	for (int axis = 0; axis < 2; axis++) {
		const double bound = CLAMP(double(position[axis]), 0.0, double(limit[axis]));
		const double offset = position[axis] - bound;
		if (!Math::is_zero_approx(offset)) {
			// Exact critically damped spring integration: stable across frame rates.
			const double omega = 18.0;
			const double decay = Math::exp(-omega * p_delta);
			const double combined = velocity[axis] + omega * offset;
			position[axis] = bound + (offset + combined * p_delta) * decay;
			velocity[axis] = (velocity[axis] - omega * combined * p_delta) * decay;
		} else {
			const double decay = Math::exp(-damping * p_delta);
			position[axis] += velocity[axis] * (1.0 - decay) / damping;
			velocity[axis] *= decay;
		}
		if (!overdrag) {
			position[axis] = CLAMP(position[axis], 0, limit[axis]);
		}
	}
	const Vector2 bounded = position.clamp(Vector2(), limit);
	if (velocity.length_squared() < 1 && position.distance_squared_to(bounded) < 0.01) {
		apply_scroll_position(bounded);
		stop_scrolling();
	} else {
		apply_scroll_position(position);
	}
}

void SmoothScrollElement::_notification(int p_what) {
	element_notification(p_what);
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS:
			process_motion(get_process_delta_time());
			break;
		case NOTIFICATION_SCROLL_BEGIN:
			if (!notifying_scroll) {
				stop_scrolling();
			}
			break;
		case NOTIFICATION_EXIT_TREE:
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT:
			stop_scrolling();
			break;
	}
}

void SmoothScrollElement::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> button = p_event;
	Ref<InputEventMouseMotion> motion = p_event;
	Ref<InputEventScreenTouch> touch = p_event;
	Ref<InputEventScreenDrag> drag = p_event;
	if ((button.is_valid() || motion.is_valid()) && p_event->get_device() == InputEvent::DEVICE_ID_EMULATION) {
		return;
	}
	if (button.is_valid() && button->is_pressed() && button->get_button_index() >= MouseButton::WHEEL_UP && button->get_button_index() <= MouseButton::WHEEL_RIGHT) {
		int axis = button->get_button_index() >= MouseButton::WHEEL_LEFT ? 0 : 1;
		if (button->is_shift_pressed()) {
			axis = 1 - axis;
		}
		if (!can_scroll_axis(axis)) {
			axis = 1 - axis;
		}
		const double direction = button->get_button_index() == MouseButton::WHEEL_UP || button->get_button_index() == MouseButton::WHEEL_LEFT ? -1.0 : 1.0;
		if (can_scroll_axis(axis) && (direction < 0 ? scroll_position[axis] > 0 : scroll_position[axis] < get_scroll_limit()[axis])) {
			velocity[axis] += direction * wheel_speed * button->get_factor();
			begin_scrolling();
			accept_event();
		}
		return;
	}
	const bool mouse_primary = button.is_valid() && button->get_button_index() == MouseButton::LEFT;
	const bool pointer_button = mouse_primary || touch.is_valid();
	const int index = touch.is_valid() ? touch->get_index() : drag.is_valid() ? drag->get_index() : -1;
	const bool pressed = pointer_button && (mouse_primary ? button->is_pressed() : touch->is_pressed());
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	Vector2 local;
	if (button.is_valid()) {
		local = button->get_position();
	} else if (motion.is_valid()) {
		local = motion->get_position();
	} else if (touch.is_valid()) {
		local = touch->get_position();
	} else if (drag.is_valid()) {
		local = drag->get_position();
	} else {
		ScrollContainer::gui_input(p_event);
		return;
	}
	if (pointer_button && pressed && pointer_index == -2) {
		if ((mouse_primary && !drag_with_mouse) || (touch.is_valid() && !drag_with_touch)) {
			return;
		}
		stop_scrolling();
		pointer_index = index;
		pointer_origin = local;
		last_pointer = local;
		drag_origin = scroll_position;
		active_axis = -1;
		velocity = Vector2();
		last_motion_time = now;
	} else if (pointer_button && !pressed && pointer_index == index) {
		pointer_index = -2;
		if (active_axis >= 0) {
			if (now - last_motion_time > 100000 || (touch.is_valid() && touch->is_canceled())) {
				velocity = Vector2();
			}
			active_axis = -1;
			begin_scrolling();
			accept_event();
		}
	} else if ((motion.is_valid() || drag.is_valid()) && pointer_index == index) {
		if (motion.is_valid() && !motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
			stop_scrolling();
			return;
		}
		const Vector2 distance = local - pointer_origin;
		if (active_axis < 0 && distance.length() >= MAX(get_deadzone(), 1)) {
			const int dominant = Math::abs(distance.x) > Math::abs(distance.y) ? 0 : 1;
			if (!can_scroll_axis(dominant)) {
				return; // Let an ancestor handle this direction.
			}
			active_axis = dominant;
			begin_scrolling();
		}
		if (active_axis >= 0) {
			const Vector2 limit = get_scroll_limit();
			Vector2 position = scroll_position;
			const int axis = active_axis;
			position[axis] = drag_origin[axis] - distance[axis];
			const double bound = CLAMP(double(position[axis]), 0.0, double(limit[axis]));
			const double excess = position[axis] - bound;
			position[axis] = overdrag ? bound + excess / (1.0 + Math::abs(excess) / 120.0) : bound;
			const double elapsed = (now - last_motion_time) / 1000000.0;
			if (elapsed > 0) {
				const double measured = CLAMP((last_pointer[axis] - local[axis]) / elapsed, -8000.0, 8000.0);
				velocity[axis] = Math::lerp(double(velocity[axis]), measured, 1.0 - Math::exp(-20.0 * elapsed));
			}
			apply_scroll_position(position);
			accept_event();
		}
		last_pointer = local;
		last_motion_time = now;
	}
}

void SmoothScrollElement::change_content_element(Control *p_content) {
	ERR_FAIL_NULL(p_content);
	ERR_FAIL_COND_MSG(p_content->get_parent(), "Scroll content must be unparented before it is added.");
	p_content->set_custom_minimum_size(p_content->get_size());
	add_child(p_content);
}

void SmoothScrollElement::set_scroll_position(const Vector2 &p_position) {
	stop_scrolling();
	apply_scroll_position(p_position.clamp(Vector2(), get_scroll_limit()));
}

void SmoothScrollElement::set_allow_vertical_scroll(bool p_enabled) {
	set_vertical_scroll_mode(p_enabled ? SCROLL_MODE_SHOW_NEVER : SCROLL_MODE_DISABLED);
}

bool SmoothScrollElement::get_allow_vertical_scroll() const {
	return get_vertical_scroll_mode() != SCROLL_MODE_DISABLED;
}

void SmoothScrollElement::set_allow_horizontal_scroll(bool p_enabled) {
	set_horizontal_scroll_mode(p_enabled ? SCROLL_MODE_SHOW_NEVER : SCROLL_MODE_DISABLED);
}

bool SmoothScrollElement::get_allow_horizontal_scroll() const {
	return get_horizontal_scroll_mode() != SCROLL_MODE_DISABLED;
}

void SmoothScrollElement::set_allow_overdrag(bool p_enabled) {
	overdrag = p_enabled;
	if (!overdrag) {
		apply_scroll_position(scroll_position.clamp(Vector2(), get_scroll_limit()));
	}
}

void SmoothScrollElement::set_wheel_speed(double p_speed) {
	ERR_FAIL_COND(p_speed < 0);
	wheel_speed = p_speed;
}

void SmoothScrollElement::set_damping(double p_damping) {
	ERR_FAIL_COND(p_damping <= 0);
	damping = p_damping;
}

SmoothScrollElement::SmoothScrollElement() {
	set_mouse_filter(MOUSE_FILTER_PASS);
	set_horizontal_scroll_mode(SCROLL_MODE_SHOW_NEVER);
	set_vertical_scroll_mode(SCROLL_MODE_SHOW_NEVER);
	set_deadzone(8);
	set_follow_focus(true);
	get_h_scroll_bar()->set_step(0);
	get_v_scroll_bar()->set_step(0);
	get_h_scroll_bar()->connect("value_changed", callable_mp(this, &SmoothScrollElement::bar_changed).bind(0));
	get_v_scroll_bar()->connect("value_changed", callable_mp(this, &SmoothScrollElement::bar_changed).bind(1));
}

void SmoothScrollElement::_bind_methods() {
	bind_element_methods<SmoothScrollElement>();
	ClassDB::bind_method(D_METHOD("change_content_element", "content"), &SmoothScrollElement::change_content_element);
	ClassDB::bind_method(D_METHOD("set_scroll_position", "position"), &SmoothScrollElement::set_scroll_position);
	ClassDB::bind_method(D_METHOD("get_scroll_position"), &SmoothScrollElement::get_scroll_position);
	ClassDB::bind_method(D_METHOD("get_is_scrolling"), &SmoothScrollElement::get_is_scrolling);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "scroll_position"), "set_scroll_position", "get_scroll_position");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_scrolling"), "", "get_is_scrolling");
	ClassDB::bind_method(D_METHOD("set_allow_vertical_scroll", "enabled"), &SmoothScrollElement::set_allow_vertical_scroll);
	ClassDB::bind_method(D_METHOD("get_allow_vertical_scroll"), &SmoothScrollElement::get_allow_vertical_scroll);
	ClassDB::bind_method(D_METHOD("set_allow_horizontal_scroll", "enabled"), &SmoothScrollElement::set_allow_horizontal_scroll);
	ClassDB::bind_method(D_METHOD("get_allow_horizontal_scroll"), &SmoothScrollElement::get_allow_horizontal_scroll);
	ClassDB::bind_method(D_METHOD("set_allow_small_vertical_scroll", "enabled"), &SmoothScrollElement::set_allow_small_vertical_scroll);
	ClassDB::bind_method(D_METHOD("get_allow_small_vertical_scroll"), &SmoothScrollElement::get_allow_small_vertical_scroll);
	ClassDB::bind_method(D_METHOD("set_allow_small_horizontal_scroll", "enabled"), &SmoothScrollElement::set_allow_small_horizontal_scroll);
	ClassDB::bind_method(D_METHOD("get_allow_small_horizontal_scroll"), &SmoothScrollElement::get_allow_small_horizontal_scroll);
	ClassDB::bind_method(D_METHOD("set_allow_overdrag", "enabled"), &SmoothScrollElement::set_allow_overdrag);
	ClassDB::bind_method(D_METHOD("get_allow_overdrag"), &SmoothScrollElement::get_allow_overdrag);
	ClassDB::bind_method(D_METHOD("set_drag_with_mouse", "enabled"), &SmoothScrollElement::set_drag_with_mouse);
	ClassDB::bind_method(D_METHOD("get_drag_with_mouse"), &SmoothScrollElement::get_drag_with_mouse);
	ClassDB::bind_method(D_METHOD("set_drag_with_touch", "enabled"), &SmoothScrollElement::set_drag_with_touch);
	ClassDB::bind_method(D_METHOD("get_drag_with_touch"), &SmoothScrollElement::get_drag_with_touch);
	ClassDB::bind_method(D_METHOD("set_wheel_speed", "speed"), &SmoothScrollElement::set_wheel_speed);
	ClassDB::bind_method(D_METHOD("get_wheel_speed"), &SmoothScrollElement::get_wheel_speed);
	ClassDB::bind_method(D_METHOD("set_damping", "damping"), &SmoothScrollElement::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &SmoothScrollElement::get_damping);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_vertical_scroll"), "set_allow_vertical_scroll", "get_allow_vertical_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_horizontal_scroll"), "set_allow_horizontal_scroll", "get_allow_horizontal_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_small_vertical_scroll"), "set_allow_small_vertical_scroll", "get_allow_small_vertical_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_small_horizontal_scroll"), "set_allow_small_horizontal_scroll", "get_allow_small_horizontal_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_overdrag"), "set_allow_overdrag", "get_allow_overdrag");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drag_with_mouse"), "set_drag_with_mouse", "get_drag_with_mouse");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drag_with_touch"), "set_drag_with_touch", "get_drag_with_touch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wheel_speed", PROPERTY_HINT_RANGE, "0,4000,1,or_greater"), "set_wheel_speed", "get_wheel_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0.1,30,0.1,or_greater"), "set_damping", "get_damping");
}
