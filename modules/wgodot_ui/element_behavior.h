// wgodot-changes::file
#pragma once

#include "scene/animation/tween.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"

// Shared implementation, with no additional node or ownership hierarchy.
// The containing native control owns this state and any presentation children.
class ElementBehavior {
	Control *control;
	Label *label = nullptr;
	ObjectID pending_parent;
	Vector2 previous_position;
	Vector2 previous_size;
	Vector2 pointer_position;
	Vector2 drag_origin;
	Vector2 control_origin;
	int pointer_index = -2;
	int movements = 0;
	bool dragging = false;
	bool enabled = true;
	bool input_disabled = false;
	bool hovering = false;
	bool hover_effect = false;
	bool hover_offset_enabled = false;
	Vector2 hover_offset_scale = Vector2(1, 1);
	int text_alignment = 5;
	Color back_color = Color(0, 0, 0, 0);
	Color fore_color = Color(1, 1, 1);
	Color image_modulate = Color(1, 1, 1);
	Ref<Texture2D> image;
	Ref<Texture2D> background;
	Vector2 bg_position;
	Ref<Tween> moving_tween;
	Ref<Tween> fading_tween;
	Ref<Tween> hover_tween;
	void update_input();
	void update_label_alignment();
	void update_hover_effect(bool p_animate = true);

public:
	explicit ElementBehavior(Control *p_control);
	void notification(int p_what);
	void rect_changed();
	void gui_input(const Ref<InputEvent> &p_event);
	void pressed();
	void animation_finished(bool p_moving);
	Label *get_label();

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_input_disabled(bool p_disabled);
	bool is_input_disabled() const { return input_disabled; }
	bool is_mouse_in() const { return hovering; }
	void set_movements(int p_movements);
	int get_movements() const { return movements; }
	void set_hover_effect(bool p_enabled);
	bool has_hover_effect() const { return hover_effect; }
	void change_parent(Node *p_parent);
	Node *get_element_parent() const;
	void center_to_screen();
	void change_position_mid_width(float p_y);

	void set_text(const String &p_text);
	void translate_text(const String &p_key, const TypedArray<String> &p_arguments);
	String get_text() const;
	void set_font(const Ref<Font> &p_font);
	Ref<Font> get_font() const;
	void set_font_size(int p_size);
	int get_font_size() const;
	void set_text_alignment(int p_alignment);
	int get_text_alignment() const { return text_alignment; }
	void set_outline_size(int p_size);
	void set_back_color(const Color &p_color);
	Color get_back_color() const { return back_color; }
	void set_fore_color(const Color &p_color);
	Color get_fore_color() const { return fore_color; }
	void set_image(const Ref<Texture2D> &p_image);
	Ref<Texture2D> get_image() const { return image; }
	void set_background(const Ref<Texture2D> &p_image);
	Ref<Texture2D> get_background() const { return background; }
	void set_image_modulate(const Color &p_color);
	Color get_image_modulate() const { return image_modulate; }
	void set_bg_position(const Vector2 &p_position);
	Vector2 get_bg_position() const { return bg_position; }
	void move_to(const Vector2 &p_position, double p_duration, double p_delay, const Callable &p_finished);
	void fade_to(double p_alpha, double p_duration, double p_delay, const Callable &p_finished);
	bool is_moving() const;
	bool is_fading() const;
	void stop_moving();
	void stop_fading();
};
