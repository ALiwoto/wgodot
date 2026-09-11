// wgodot-changes::file
#pragma once

#include "element_control.h"
#include "scene/gui/scroll_container.h"

class SmoothScrollElement : public ElementControl<ScrollContainer> {
	GDCLASS(SmoothScrollElement, ScrollContainer);

	Vector2 scroll_position;
	Vector2 velocity;
	Vector2 pointer_origin;
	Vector2 last_pointer;
	Vector2 drag_origin;
	uint64_t last_motion_time = 0;
	int pointer_index = -2;
	int active_axis = -1;
	bool scrolling = false;
	bool notifying_scroll = false;
	bool updating_bars = false;
	bool overdrag = true;
	bool small_vertical = false;
	bool small_horizontal = false;
	bool drag_with_mouse = true;
	bool drag_with_touch = true;
	double wheel_speed = 500.0;
	double damping = 8.0;

	Vector2 get_scroll_limit() const;
	bool can_scroll_axis(int p_axis) const;
	void apply_scroll_position(const Vector2 &p_position);
	void bar_changed(double p_value, int p_axis);
	void begin_scrolling();
	void stop_scrolling();
	void process_motion(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	Vector2 _wgodot_get_scroll_displacement() const override;

public:
	void gui_input(const Ref<InputEvent> &p_event) override;
	void change_content_element(Control *p_content);
	void set_scroll_position(const Vector2 &p_position);
	Vector2 get_scroll_position() const { return scroll_position; }
	bool get_is_scrolling() const { return scrolling; }
	void set_allow_vertical_scroll(bool p_enabled);
	bool get_allow_vertical_scroll() const;
	void set_allow_horizontal_scroll(bool p_enabled);
	bool get_allow_horizontal_scroll() const;
	void set_allow_small_vertical_scroll(bool p_enabled) { small_vertical = p_enabled; }
	bool get_allow_small_vertical_scroll() const { return small_vertical; }
	void set_allow_small_horizontal_scroll(bool p_enabled) { small_horizontal = p_enabled; }
	bool get_allow_small_horizontal_scroll() const { return small_horizontal; }
	void set_allow_overdrag(bool p_enabled);
	bool get_allow_overdrag() const { return overdrag; }
	void set_drag_with_mouse(bool p_enabled) { drag_with_mouse = p_enabled; }
	bool get_drag_with_mouse() const { return drag_with_mouse; }
	void set_drag_with_touch(bool p_enabled) { drag_with_touch = p_enabled; }
	bool get_drag_with_touch() const { return drag_with_touch; }
	void set_wheel_speed(double p_speed);
	double get_wheel_speed() const { return wheel_speed; }
	void set_damping(double p_damping);
	double get_damping() const { return damping; }
	SmoothScrollElement();
};
