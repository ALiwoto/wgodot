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

	struct VirtualGrid;
	VirtualGrid *virtual_grid = nullptr;
	bool virtual_update_pending = false;
	bool virtual_focus_changing = false;
	bool virtual_focus_inside = false;
	void queue_virtual_update();
	void connect_virtual_focus();
	void virtual_focus_changed(Control *p_focus);
	void update_virtual_grid();
	void virtual_notification(int p_what);
	bool virtual_gui_input(const Ref<InputEvent> &p_event);
	bool focus_outside_virtual_content(bool p_forward);
	void virtual_item_gui_input(const Ref<InputEvent> &p_event, Control *p_view);
	void update_virtual_accessibility();
	void virtual_accessibility_action(const Variant &p_data, int p_index, bool p_focus);
	static void bind_virtual_methods();

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
	// Uniform vertical grid. The content must be a direct child with no other items.
	// create(size) returns an unparented Control; bind(view, index) restores all item
	// state; unbind(view, old_index) releases subscriptions and pending asset work.
	void set_virtual_content(Control *p_content, const Callable &p_create, const Callable &p_bind, const Callable &p_unbind);
	void clear_virtual_content();
	// item_info(index, RID) updates the logical item through AccessibilityServer.
	void set_virtual_accessibility(const Callable &p_item_info);
	bool accessibility_override_tree_hierarchy() const override { return virtual_grid != nullptr; }
	void set_virtual_grid(int p_count, int p_columns, const Vector2 &p_item_size, const Vector2 &p_separation = Vector2(), const Vector4 &p_margins = Vector4());
	void set_virtual_buffer_rows(int p_rows);
	int get_virtual_buffer_rows() const;
	void refresh_virtual_items();
	void refresh_virtual_item(int p_index);
	Control *get_virtual_item(int p_index) const;
	int get_virtual_item_index(const Control *p_item) const;
	TypedArray<Control> get_virtual_items() const;
	int get_virtual_item_count() const;
	int get_virtual_pool_size() const;
	void scroll_to_virtual_item(int p_index);
	void focus_virtual_item(int p_index);
	SmoothScrollElement();
	~SmoothScrollElement();
};
