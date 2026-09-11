// wgodot-changes::file
#pragma once

#include "element_behavior.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

// C++ sharing only: ClassDB still sees each concrete Control/Button/LineEdit parent.
template <class NativeControl>
class ElementControl : public NativeControl {
protected:
	ElementBehavior element;
	void element_notification(int p_what) { element.notification(p_what); }
	void _rect_changed() { element.rect_changed(); }
	void _pressed() { element.pressed(); }
	void _moving_finished() { element.animation_finished(true); }
	void _fading_finished() { element.animation_finished(false); }

	template <class Derived, class Name, class R, class... Args, class... Defaults>
	static void bind_element_method(const Name &p_name, R (ElementControl::*p_method)(Args...), Defaults... p_defaults) {
		ClassDB::bind_method(p_name, static_cast<R (Derived::*)(Args...)>(p_method), p_defaults...);
	}
	template <class Derived, class Name, class R, class... Args, class... Defaults>
	static void bind_element_method(const Name &p_name, R (ElementControl::*p_method)(Args...) const, Defaults... p_defaults) {
		ClassDB::bind_method(p_name, static_cast<R (Derived::*)(Args...) const>(p_method), p_defaults...);
	}

	template <class Derived>
	static void bind_element_methods() {
		bind_element_method<Derived>(D_METHOD("change_text_translation", "key", "arguments"), &ElementControl::change_text_translation, DEFVAL(TypedArray<String>()));
		bind_element_method<Derived>(D_METHOD("append_text", "text"), &ElementControl::append_text);
		bind_element_method<Derived>(D_METHOD("change_parent", "parent"), &ElementControl::change_parent);
		bind_element_method<Derived>(D_METHOD("get_element_parent"), &ElementControl::get_element_parent);
		bind_element_method<Derived>(D_METHOD("change_position", "x", "y"), &ElementControl::change_position);
		bind_element_method<Derived>(D_METHOD("change_position_vector", "position"), &ElementControl::change_position_vector);
		bind_element_method<Derived>(D_METHOD("get_element_position"), &ElementControl::get_element_position);
		bind_element_method<Derived>(D_METHOD("change_element_size", "width", "height"), &ElementControl::change_element_size);
		bind_element_method<Derived>(D_METHOD("change_element_size_vector", "size"), &ElementControl::change_element_size_vector);
		bind_element_method<Derived>(D_METHOD("get_element_size"), &ElementControl::get_element_size);
		bind_element_method<Derived>(D_METHOD("get_element_rect"), &ElementControl::get_element_rect);
		bind_element_method<Derived>(D_METHOD("set_element_enabled", "enabled"), &ElementControl::set_element_enabled);
		bind_element_method<Derived>(D_METHOD("is_element_enabled"), &ElementControl::is_element_enabled);
		bind_element_method<Derived>(D_METHOD("enable_element"), &ElementControl::enable_element);
		bind_element_method<Derived>(D_METHOD("disable_element"), &ElementControl::disable_element);
		bind_element_method<Derived>(D_METHOD("set_input_handling_disabled", "disabled"), &ElementControl::set_input_handling_disabled);
		bind_element_method<Derived>(D_METHOD("get_input_handling_disabled"), &ElementControl::get_input_handling_disabled);
		bind_element_method<Derived>(D_METHOD("enable_input_handling"), &ElementControl::enable_input_handling);
		bind_element_method<Derived>(D_METHOD("disable_input_handling"), &ElementControl::disable_input_handling);
		bind_element_method<Derived>(D_METHOD("is_element_hovered"), &ElementControl::is_element_hovered);
		bind_element_method<Derived>(D_METHOD("change_movements", "movements"), &ElementControl::change_movements);
		bind_element_method<Derived>(D_METHOD("get_movement_mode"), &ElementControl::get_movement_mode);
		bind_element_method<Derived>(D_METHOD("change_text", "text", "update_position"), &ElementControl::change_text, DEFVAL(true));
		bind_element_method<Derived>(D_METHOD("get_element_text"), &ElementControl::get_element_text);
		bind_element_method<Derived>(D_METHOD("set_element_text", "text"), &ElementControl::set_element_text);
		bind_element_method<Derived>(D_METHOD("change_font", "font"), &ElementControl::change_font);
		bind_element_method<Derived>(D_METHOD("get_element_font"), &ElementControl::get_element_font);
		bind_element_method<Derived>(D_METHOD("change_font_size", "size"), &ElementControl::change_font_size);
		bind_element_method<Derived>(D_METHOD("get_element_font_size"), &ElementControl::get_element_font_size);
		bind_element_method<Derived>(D_METHOD("change_text_alignment", "alignment"), &ElementControl::change_text_alignment);
		bind_element_method<Derived>(D_METHOD("get_element_text_alignment"), &ElementControl::get_element_text_alignment);
		bind_element_method<Derived>(D_METHOD("change_outline_size", "size"), &ElementControl::change_outline_size);
		bind_element_method<Derived>(D_METHOD("change_back_color", "color"), &ElementControl::change_back_color);
		bind_element_method<Derived>(D_METHOD("get_element_back_color"), &ElementControl::get_element_back_color);
		bind_element_method<Derived>(D_METHOD("change_fore_color", "color"), &ElementControl::change_fore_color);
		bind_element_method<Derived>(D_METHOD("get_element_fore_color"), &ElementControl::get_element_fore_color);
		bind_element_method<Derived>(D_METHOD("change_image", "image"), &ElementControl::change_image);
		bind_element_method<Derived>(D_METHOD("get_element_image"), &ElementControl::get_element_image);
		bind_element_method<Derived>(D_METHOD("set_element_background", "image"), &ElementControl::set_element_background);
		bind_element_method<Derived>(D_METHOD("get_element_background"), &ElementControl::get_element_background);
		bind_element_method<Derived>(D_METHOD("change_image_modulate", "color"), &ElementControl::change_image_modulate);
		bind_element_method<Derived>(D_METHOD("get_element_image_modulate"), &ElementControl::get_element_image_modulate);
		bind_element_method<Derived>(D_METHOD("change_bg_position_vector", "position"), &ElementControl::change_bg_position_vector);
		bind_element_method<Derived>(D_METHOD("get_element_bg_position"), &ElementControl::get_element_bg_position);
		bind_element_method<Derived>(D_METHOD("move_element_to", "position", "duration", "delay"), &ElementControl::move_element_to, DEFVAL(0.9), DEFVAL(0.4));
		bind_element_method<Derived>(D_METHOD("fade_element_to", "alpha", "duration", "delay"), &ElementControl::fade_element_to, DEFVAL(0.9), DEFVAL(0.0));
		bind_element_method<Derived>(D_METHOD("get_is_element_moving"), &ElementControl::get_is_element_moving);
		bind_element_method<Derived>(D_METHOD("get_is_element_fading"), &ElementControl::get_is_element_fading);
		bind_element_method<Derived>(D_METHOD("stop_moving_element"), &ElementControl::stop_moving_element);
		bind_element_method<Derived>(D_METHOD("stop_fading_element"), &ElementControl::stop_fading_element);
		bind_element_method<Derived>(D_METHOD("enable_mouse_enter_effect"), &ElementControl::enable_mouse_enter_effect);
		bind_element_method<Derived>(D_METHOD("disable_mouse_enter_effect"), &ElementControl::disable_mouse_enter_effect);
		bind_element_method<Derived>(D_METHOD("center_to_screen"), &ElementControl::center_to_screen);
		bind_element_method<Derived>(D_METHOD("change_position_mid_width", "y"), &ElementControl::change_position_mid_width);
		bind_element_method<Derived>(D_METHOD("get_element_width"), &ElementControl::get_element_width);
		bind_element_method<Derived>(D_METHOD("get_element_height"), &ElementControl::get_element_height);
		bind_element_method<Derived>(D_METHOD("get_element_bottom"), &ElementControl::get_element_bottom);
		bind_element_method<Derived>(D_METHOD("get_element_bottom_left"), &ElementControl::get_element_bottom_left);
		bind_element_method<Derived>(D_METHOD("viewport_to_local_position", "position"), &ElementControl::viewport_to_local_position);
		bind_element_method<Derived>(D_METHOD("is_position_acceptable", "position"), &ElementControl::is_position_acceptable);
		bind_element_method<Derived>(D_METHOD("update"), &ElementControl::update);
		const StringName class_name = Derived::get_class_static();
		ClassDB::add_property(class_name, PropertyInfo(Variant::VECTOR2, "element_size"), "change_element_size_vector", "get_element_size");
		ClassDB::add_property(class_name, PropertyInfo(Variant::VECTOR2, "element_position"), "change_position_vector", "get_element_position");
		ClassDB::add_property(class_name, PropertyInfo(Variant::RECT2, "element_rect"), "", "get_element_rect");
		ClassDB::add_property(class_name, PropertyInfo(Variant::OBJECT, "element_parent", PROPERTY_HINT_NODE_TYPE, "Node"), "change_parent", "get_element_parent");
		ClassDB::add_property(class_name, PropertyInfo(Variant::BOOL, "enabled"), "set_element_enabled", "is_element_enabled");
		ClassDB::add_property(class_name, PropertyInfo(Variant::BOOL, "is_input_handling_disabled"), "set_input_handling_disabled", "get_input_handling_disabled");
		ClassDB::add_property(class_name, PropertyInfo(Variant::BOOL, "is_mouse_in"), "", "is_element_hovered");
		ClassDB::add_property(class_name, PropertyInfo(Variant::INT, "element_movements"), "change_movements", "get_movement_mode");
		ClassDB::add_property(class_name, PropertyInfo(Variant::STRING, "element_text"), "set_element_text", "get_element_text");
		ClassDB::add_property(class_name, PropertyInfo(Variant::OBJECT, "element_font", PROPERTY_HINT_RESOURCE_TYPE, "Font"), "change_font", "get_element_font");
		ClassDB::add_property(class_name, PropertyInfo(Variant::INT, "element_font_size"), "change_font_size", "get_element_font_size");
		ClassDB::add_property(class_name, PropertyInfo(Variant::INT, "element_text_align"), "change_text_alignment", "get_element_text_alignment");
		ClassDB::add_property(class_name, PropertyInfo(Variant::COLOR, "element_back_color"), "change_back_color", "get_element_back_color");
		ClassDB::add_property(class_name, PropertyInfo(Variant::COLOR, "element_fore_color"), "change_fore_color", "get_element_fore_color");
		ClassDB::add_property(class_name, PropertyInfo(Variant::OBJECT, "element_image", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "change_image", "get_element_image");
		ClassDB::add_property(class_name, PropertyInfo(Variant::OBJECT, "element_background", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_element_background", "get_element_background");
		ClassDB::add_property(class_name, PropertyInfo(Variant::COLOR, "element_image_modulate"), "change_image_modulate", "get_element_image_modulate");
		ClassDB::add_property(class_name, PropertyInfo(Variant::VECTOR2, "element_bg_position"), "change_bg_position_vector", "get_element_bg_position");
		ClassDB::add_property(class_name, PropertyInfo(Variant::BOOL, "is_element_moving"), "", "get_is_element_moving");
		ClassDB::add_property(class_name, PropertyInfo(Variant::BOOL, "is_element_fading"), "", "get_is_element_fading");
		ClassDB::add_signal(class_name, MethodInfo("mouse_enter", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("mouse_leave", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("left_down", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("left_up", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("left_click", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("clicked", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "position")));
		ClassDB::add_signal(class_name, MethodInfo("on_element_moving_finished", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase")));
		ClassDB::add_signal(class_name, MethodInfo("on_element_fading_finished", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase")));
		ClassDB::add_signal(class_name, MethodInfo("on_element_moving_stopped", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase")));
		ClassDB::add_signal(class_name, MethodInfo("on_element_fading_stopped", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase")));
		ClassDB::add_signal(class_name, MethodInfo("position_changed", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "new_position")));
		ClassDB::add_signal(class_name, MethodInfo("size_changed", PropertyInfo(Variant::OBJECT, "element", PROPERTY_HINT_RESOURCE_TYPE, "ElementBase"), PropertyInfo(Variant::VECTOR2, "new_size")));
		ClassDB::bind_integer_constant(class_name, "MovementMode", "NoMovements", 0);
		ClassDB::bind_integer_constant(class_name, "MovementMode", "VerticalMovements", 1);
		ClassDB::bind_integer_constant(class_name, "MovementMode", "HorizontalMovements", 2);
		ClassDB::bind_integer_constant(class_name, "MovementMode", "VerticalHorizontalMovements", 3);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "NaN", 0);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "TopLeft", 1);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "TopCenter", 2);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "TopRight", 3);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "MiddleLeft", 4);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "MiddleCenter", 5);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "MiddleRight", 7);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "BottomLeft", 8);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "BottomCenter", 9);
		ClassDB::bind_integer_constant(class_name, "TextAlign", "BottomRight", 10);
	}

public:
	void change_text_translation(const String &p_key, const TypedArray<String> &p_arguments = TypedArray<String>()) { element.translate_text(p_key, p_arguments); }
	void append_text(const String &p_text) { element.set_text(element.get_text() + p_text); }
	ElementControl() : element(this) {
		this->connect("item_rect_changed", callable_mp(this, &ElementControl::_rect_changed));
		if constexpr (std::is_base_of_v<BaseButton, NativeControl>) {
			this->connect("pressed", callable_mp(this, &ElementControl::_pressed));
		}
	}
	void gui_input(const Ref<InputEvent> &p_event) override {
		element.gui_input(p_event);
		NativeControl::gui_input(p_event);
	}
	void change_parent(Node *p_parent) { element.change_parent(p_parent); }
	Node * get_element_parent() const { return element.get_element_parent(); }
	void change_position(float p_x, float p_y) { this->set_position(Vector2(p_x, p_y)); }
	void change_position_vector(const Vector2 &p_position) { this->set_position(p_position); }
	Vector2 get_element_position() const { return this->get_position(); }
	void change_element_size(float p_width, float p_height) { this->set_size(Vector2(p_width, p_height)); }
	void change_element_size_vector(const Vector2 &p_size) { this->set_size(p_size); }
	Vector2 get_element_size() const { return this->get_size(); }
	Rect2 get_element_rect() const { return Rect2(Vector2(), this->get_size()); }
	void set_element_enabled(bool p_enabled) { element.set_enabled(p_enabled); }
	bool is_element_enabled() const { return element.is_enabled(); }
	void enable_element() { element.set_enabled(true); }
	void disable_element() { element.set_enabled(false); }
	void set_input_handling_disabled(bool p_disabled) { element.set_input_disabled(p_disabled); }
	bool get_input_handling_disabled() const { return element.is_input_disabled(); }
	void enable_input_handling() { element.set_input_disabled(false); }
	void disable_input_handling() { element.set_input_disabled(true); }
	bool is_element_hovered() const { return element.is_mouse_in(); }
	void change_movements(int p_movements) { element.set_movements(p_movements); }
	int get_movement_mode() const { return element.get_movements(); }
	void change_text(const String &p_text, bool p_update_position = true) { element.set_text(p_text); }
	String get_element_text() const { return element.get_text(); }
	void set_element_text(const String &p_text) { element.set_text(p_text); }
	void change_font(const Ref<Font> &p_font) { element.set_font(p_font); }
	Ref<Font> get_element_font() const { return element.get_font(); }
	void change_font_size(int p_size) { element.set_font_size(p_size); }
	int get_element_font_size() const { return element.get_font_size(); }
	void change_text_alignment(int p_alignment) { element.set_text_alignment(p_alignment); }
	int get_element_text_alignment() const { return element.get_text_alignment(); }
	void change_outline_size(int p_size) { element.set_outline_size(p_size); }
	void change_back_color(const Color &p_color) { element.set_back_color(p_color); }
	Color get_element_back_color() const { return element.get_back_color(); }
	void change_fore_color(const Color &p_color) { element.set_fore_color(p_color); }
	Color get_element_fore_color() const { return element.get_fore_color(); }
	void change_image(const Ref<Texture2D> &p_image) { element.set_image(p_image); }
	Ref<Texture2D> get_element_image() const { return element.get_image(); }
	void set_element_background(const Ref<Texture2D> &p_image) { element.set_background(p_image); }
	Ref<Texture2D> get_element_background() const { return element.get_background(); }
	void change_image_modulate(const Color &p_color) { element.set_image_modulate(p_color); }
	Color get_element_image_modulate() const { return element.get_image_modulate(); }
	void change_bg_position_vector(const Vector2 &p_position) { element.set_bg_position(p_position); }
	Vector2 get_element_bg_position() const { return element.get_bg_position(); }
	void move_element_to(const Vector2 &p_position, double p_duration = 0.9, double p_delay = 0.4) { element.move_to(p_position, p_duration, p_delay, callable_mp(this, &ElementControl::_moving_finished)); }
	void fade_element_to(double p_alpha, double p_duration = 0.9, double p_delay = 0.0) { element.fade_to(p_alpha, p_duration, p_delay, callable_mp(this, &ElementControl::_fading_finished)); }
	bool get_is_element_moving() const { return element.is_moving(); }
	bool get_is_element_fading() const { return element.is_fading(); }
	void stop_moving_element() { element.stop_moving(); }
	void stop_fading_element() { element.stop_fading(); }
	void enable_mouse_enter_effect() { element.set_hover_effect(true); }
	void disable_mouse_enter_effect() { element.set_hover_effect(false); }
	void center_to_screen() { element.center_to_screen(); }
	void change_position_mid_width(float p_y) { element.change_position_mid_width(p_y); }
	float get_element_width() const { return this->get_size().x; }
	float get_element_height() const { return this->get_size().y; }
	float get_element_bottom() const { return this->get_position().y + this->get_size().y; }
	Vector2 get_element_bottom_left() const { return this->get_position() + Vector2(0, this->get_size().y); }
	Vector2 viewport_to_local_position(const Vector2 &p_position) const { return this->get_global_transform_with_canvas().affine_inverse().xform(p_position); }
	bool is_position_acceptable(const Vector2 &p_position) const { return this->has_point(viewport_to_local_position(p_position)); }
	void update() { this->queue_redraw(); }
};
