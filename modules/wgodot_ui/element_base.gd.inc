// wgodot-changes::file
R"(
interface_name ElementBase
extends Control

func change_text_translation(p_key: String, p_arguments: Array[String] = []) -> void
func append_text(p_text: String) -> void

enum MovementMode { NoMovements = 0, VerticalMovements = 1, HorizontalMovements = 2, VerticalHorizontalMovements = 3 }
enum TextAlign { NaN = 0, TopLeft = 1, TopCenter = 2, TopRight = 3, MiddleLeft = 4, MiddleCenter = 5, MiddleRight = 7, BottomLeft = 8, BottomCenter = 9, BottomRight = 10 }

var element_size: Vector2
var element_position: Vector2
@readonly var element_rect: Rect2
var element_parent: Node
var enabled: bool
var is_input_handling_disabled: bool
@readonly var is_mouse_in: bool
var element_movements: int
var element_text: String
var element_font: Font
var element_font_size: int
var element_text_align: int
var element_back_color: Color
var element_fore_color: Color
var element_image: Texture2D
var element_background: Texture2D
var element_image_modulate: Color
var element_bg_position: Vector2
@readonly var is_element_moving: bool
@readonly var is_element_fading: bool

func change_parent(p_parent:Node) -> void
func get_element_parent() -> Node
func change_position(p_x:float, p_y:float) -> void
func change_position_vector(p_position:Vector2) -> void
func get_element_position() -> Vector2
func change_element_size(p_width:float, p_height:float) -> void
func change_element_size_vector(p_size:Vector2) -> void
func get_element_size() -> Vector2
func get_element_rect() -> Rect2
func set_element_enabled(p_enabled:bool) -> void
func is_element_enabled() -> bool
func enable_element() -> void
func disable_element() -> void
func set_input_handling_disabled(p_disabled:bool) -> void
func enable_input_handling() -> void
func disable_input_handling() -> void
func change_movements(p_movements:int) -> void
func get_movement_mode() -> int
func change_text(p_text:String, p_update_position:bool=true) -> void
func get_element_text() -> String
func set_element_text(p_text:String) -> void
func change_font(p_font:Font) -> void
func get_element_font() -> Font
func change_font_size(p_size:int) -> void
func get_element_font_size() -> int
func change_text_alignment(p_alignment:int) -> void
func get_element_text_alignment() -> int
func change_outline_size(p_size:int) -> void
func change_back_color(p_color:Color) -> void
func get_element_back_color() -> Color
func change_fore_color(p_color:Color) -> void
func get_element_fore_color() -> Color
func change_image(p_image:Texture2D) -> void
func get_element_image() -> Texture2D
func set_element_background(p_image:Texture2D) -> void
func get_element_background() -> Texture2D
func change_image_modulate(p_color:Color) -> void
func get_element_image_modulate() -> Color
func change_bg_position_vector(p_position:Vector2) -> void
func get_element_bg_position() -> Vector2
func move_element_to(p_position:Vector2, p_duration:float=0.9, p_delay:float=0.4) -> void
func fade_element_to(p_alpha:float, p_duration:float=0.9, p_delay:float=0.0) -> void
func stop_moving_element() -> void
func stop_fading_element() -> void
func enable_mouse_enter_effect() -> void
func disable_mouse_enter_effect() -> void
func center_to_screen() -> void
func change_position_mid_width(p_y:float) -> void
func get_element_width() -> float
func get_element_height() -> float
func get_element_bottom() -> float
func get_element_bottom_left() -> Vector2
func viewport_to_local_position(p_position:Vector2) -> Vector2
func is_position_acceptable(p_position:Vector2) -> bool
func update() -> void
signal mouse_enter(element:ElementBase, position:Vector2)
signal mouse_leave(element:ElementBase, position:Vector2)
signal left_down(element:ElementBase, position:Vector2)
signal left_up(element:ElementBase, position:Vector2)
signal left_click(element:ElementBase, position:Vector2)
signal clicked(element:ElementBase, position:Vector2)
signal on_element_moving_finished(element:ElementBase)
signal on_element_fading_finished(element:ElementBase)
signal on_element_moving_stopped(element:ElementBase)
signal on_element_fading_stopped(element:ElementBase)
signal position_changed(element:ElementBase, position:Vector2)
signal size_changed(element:ElementBase, position:Vector2)
)"
