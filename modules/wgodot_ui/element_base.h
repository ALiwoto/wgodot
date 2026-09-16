// wgodot-changes::file
#pragma once

#include "core/object/wgodot_interface.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"
#include "scene/resources/font.h"
#include "scene/resources/texture.h"

class ElementBase {
	WGD_INTERFACE(ElementBase);

public:
	virtual void change_text_translation(const String &p_key, const TypedArray<String> &p_arguments = TypedArray<String>()) = 0;
	virtual void append_text(const String &p_text) = 0;
	virtual void change_parent(Node *p_parent) = 0;
	virtual Node *get_element_parent() const = 0;
	virtual void change_position(float p_x, float p_y) = 0;
	virtual void change_position_vector(const Vector2 &p_position) = 0;
	virtual Vector2 get_element_position() const = 0;
	virtual void change_element_size(float p_width, float p_height) = 0;
	virtual void change_element_size_vector(const Vector2 &p_size) = 0;
	virtual Vector2 get_element_size() const = 0;
	virtual Rect2 get_element_rect() const = 0;
	virtual void set_element_enabled(bool p_enabled) = 0;
	virtual bool is_element_enabled() const = 0;
	virtual void enable_element() = 0;
	virtual void disable_element() = 0;
	virtual void set_input_handling_disabled(bool p_disabled) = 0;
	virtual bool get_input_handling_disabled() const = 0;
	virtual void enable_input_handling() = 0;
	virtual void disable_input_handling() = 0;
	virtual bool is_element_hovered() const = 0;
	virtual void change_movements(int p_movements) = 0;
	virtual int get_movement_mode() const = 0;
	virtual void change_text(const String &p_text, bool p_update_position = true) = 0;
	virtual String get_element_text() const = 0;
	virtual void set_element_text(const String &p_text) = 0;
	virtual void change_font(const Ref<Font> &p_font) = 0;
	virtual Ref<Font> get_element_font() const = 0;
	virtual void change_font_size(int p_size) = 0;
	virtual int get_element_font_size() const = 0;
	virtual void change_text_alignment(int p_alignment) = 0;
	virtual int get_element_text_alignment() const = 0;
	virtual void change_outline_size(int p_size) = 0;
	virtual void change_back_color(const Color &p_color) = 0;
	virtual Color get_element_back_color() const = 0;
	virtual void change_fore_color(const Color &p_color) = 0;
	virtual Color get_element_fore_color() const = 0;
	virtual void change_image(const Ref<Texture2D> &p_image) = 0;
	virtual Ref<Texture2D> get_element_image() const = 0;
	virtual void set_element_background(const Ref<Texture2D> &p_image) = 0;
	virtual Ref<Texture2D> get_element_background() const = 0;
	virtual void change_image_modulate(const Color &p_color) = 0;
	virtual Color get_element_image_modulate() const = 0;
	virtual void change_bg_position_vector(const Vector2 &p_position) = 0;
	virtual Vector2 get_element_bg_position() const = 0;
	virtual void move_element_to(const Vector2 &p_position, double p_duration = 0.9, double p_delay = 0.4) = 0;
	virtual void fade_element_to(double p_alpha, double p_duration = 0.9, double p_delay = 0.0) = 0;
	virtual bool get_is_element_moving() const = 0;
	virtual bool get_is_element_fading() const = 0;
	virtual void stop_moving_element() = 0;
	virtual void stop_fading_element() = 0;
	virtual void enable_mouse_enter_effect() = 0;
	virtual void disable_mouse_enter_effect() = 0;
	virtual void center_to_screen() = 0;
	virtual void change_position_mid_width(float p_y) = 0;
	virtual float get_element_width() const = 0;
	virtual float get_element_height() const = 0;
	virtual float get_element_bottom() const = 0;
	virtual Vector2 get_element_bottom_left() const = 0;
	virtual Vector2 viewport_to_local_position(const Vector2 &p_position) const = 0;
	virtual bool is_position_acceptable(const Vector2 &p_position) const = 0;
	virtual void update() = 0;
};
