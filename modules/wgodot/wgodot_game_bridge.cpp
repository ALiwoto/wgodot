// wgodot-changes::file
/**************************************************************************/
/*  wgodot_game_bridge.cpp                                                */
/**************************************************************************/

#include "wgodot_game_bridge.h"

#include "core/debugger/engine_debugger.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/vector.h"
#include "scene/gui/control.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

namespace WGodotGameBridge {

#ifdef DEBUG_ENABLED

namespace {

bool capture_registered = false;

Dictionary make_error(const String &p_command, const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

bool validate_filter_types(const PackedStringArray &p_types, const String &p_option, Dictionary &r_error) {
	for (int i = 0; i < p_types.size(); i++) {
		const StringName type = p_types[i];
		if (type != SNAME("*") && !ClassDB::class_exists(type) && !ScriptServer::is_global_class(type)) {
			r_error = make_error("tree", "invalid_type_filter", "Unknown type for " + p_option + ": " + String(type));
			return false;
		}
	}
	return true;
}

bool node_matches_type(Node *p_node, const StringName &p_type) {
	if (p_type == SNAME("*") || (ClassDB::class_exists(p_type) && p_node->is_class(p_type))) {
		return true;
	}

	Ref<Script> script = p_node->get_script();
	if (script.is_null()) {
		return false;
	}
	StringName script_type = script->get_global_name();
	while (!script_type.is_empty() && ScriptServer::is_global_class(script_type)) {
		if (script_type == p_type) {
			return true;
		}
		script_type = ScriptServer::get_global_class_base(script_type);
	}
	return false;
}

bool node_matches_any_type(Node *p_node, const PackedStringArray &p_types) {
	for (int i = 0; i < p_types.size(); i++) {
		if (node_matches_type(p_node, p_types[i])) {
			return true;
		}
	}
	return false;
}

String get_node_display_type(Node *p_node) {
	Ref<Script> script = p_node->get_script();
	while (script.is_valid()) {
		const StringName global_name = script->get_global_name();
		if (!global_name.is_empty()) {
			return global_name;
		}
#ifdef MODULE_GDSCRIPT_ENABLED
		const Ref<GDScript> gdscript = script;
		if (gdscript.is_valid() && !gdscript->get_local_name().is_empty()) {
			return gdscript->get_local_name();
		}
#endif
		script = script->get_base_script();
	}
	return p_node->get_class();
}

Array collect_tree(const Dictionary &p_options, Dictionary &r_error) {
	Array result;
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree == nullptr || scene_tree->get_root() == nullptr) {
		r_error = make_error("tree", "scene_tree_unavailable", "The running game has no scene tree.");
		return result;
	}

	Node *root = scene_tree->get_root();
	const String requested_root = p_options.get("root", String());
	if (!requested_root.is_empty()) {
		root = root->get_node_or_null(NodePath(requested_root));
		if (root == nullptr) {
			r_error = make_error("tree", "node_not_found", "Tree root was not found: " + requested_root);
			return result;
		}
	}

	const int max_depth = p_options.get("max_depth", -1);
	PackedStringArray include_types = p_options.get("include_types", PackedStringArray());
	const PackedStringArray exclude_types = p_options.get("exclude_types", PackedStringArray());
	if (include_types.is_empty()) {
		include_types.push_back("*");
	}
	if (!validate_filter_types(include_types, "--include", r_error) || !validate_filter_types(exclude_types, "--exclude", r_error)) {
		return result;
	}

	struct PendingNode {
		Node *node = nullptr;
		int depth = 0;
	};
	Vector<PendingNode> stack;
	stack.push_back({ root, 0 });
	while (!stack.is_empty()) {
		const PendingNode pending = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		Node *node = pending.node;

		if (node_matches_any_type(node, include_types) && !node_matches_any_type(node, exclude_types)) {
			Dictionary entry;
			entry["depth"] = pending.depth;
			entry["path"] = String(node->get_path());
			entry["name"] = String(node->get_name());
			entry["type"] = get_node_display_type(node);
			entry["id"] = static_cast<int64_t>(node->get_instance_id());
			entry["child_count"] = node->get_child_count();
			entry["scene_file_path"] = node->get_scene_file_path();
			if (node->has_method(SNAME("is_visible"))) {
				const Variant visible = node->call(SNAME("is_visible"));
				if (visible.get_type() == Variant::BOOL) {
					entry["visible"] = visible;
				}
			}
			if (node->has_method(SNAME("is_visible_in_tree"))) {
				const Variant visible_in_tree = node->call(SNAME("is_visible_in_tree"));
				if (visible_in_tree.get_type() == Variant::BOOL) {
					entry["visible_in_tree"] = visible_in_tree;
				}
			}
			result.push_back(entry);
		}

		if (max_depth >= 0 && pending.depth >= max_depth) {
			continue;
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back({ node->get_child(i), pending.depth + 1 });
		}
	}

	return result;
}

Dictionary take_screenshot(const Dictionary &p_options) {
	Viewport *viewport = SceneTree::get_singleton() ? SceneTree::get_singleton()->get_root() : nullptr;
	if (viewport == nullptr) {
		return make_error("ss", "viewport_unavailable", "The running game has no root viewport.");
	}

	Ref<ViewportTexture> texture = viewport->get_texture();
	if (texture.is_null()) {
		return make_error("ss", "viewport_unavailable", "The running game viewport has no texture.");
	}
	Ref<Image> image = texture->get_image();
	if (image.is_null()) {
		return make_error("ss", "screenshot_failed", "The running game viewport could not be captured.");
	}
	image->clear_mipmaps();
	image->convert(Image::FORMAT_RGBA8);
#ifdef RD_ENABLED
	RenderingDevice *rendering_device = RD::get_singleton();
	if (rendering_device && RenderingServer::get_singleton()->viewport_is_using_hdr_2d(viewport->get_viewport_rid())) {
		image->linear_to_srgb();
	}
#endif

	String path = p_options.get("output", String());
	if (path.is_empty()) {
		const String timestamp = Time::get_singleton()->get_datetime_string_from_system().remove_chars("-T:") + itos(OS::get_singleton()->get_ticks_usec());
		path = OS::get_singleton()->get_temp_path().path_join("wgodot-ss-" + timestamp + ".png");
	} else if (path.is_absolute_path()) {
		const Error directory_error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		if (directory_error != OK) {
			return make_error("ss", "output_directory_failed", "Could not create the screenshot output directory: " + path.get_base_dir());
		}
	}

	const Error save_error = image->save_png(path);
	if (save_error != OK) {
		return make_error("ss", "screenshot_save_failed", "Could not save the screenshot: " + path);
	}

	Dictionary screenshot;
	screenshot["path"] = path;
	screenshot["width"] = image->get_width();
	screenshot["height"] = image->get_height();
	return screenshot;
}

Window *find_keyboard_window() {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Window *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (root == nullptr) {
		return nullptr;
	}

	Vector<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		Viewport *viewport = Object::cast_to<Viewport>(node);
		if (viewport && viewport->gui_get_focus_owner()) {
			return viewport->gui_get_focus_owner()->get_window();
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(node->get_child(i));
		}
	}
	return root;
}

void send_key_event(Key p_keycode, char32_t p_unicode, bool p_pressed, int64_t p_window_id) {
	Ref<InputEventKey> event = InputEventKey::create_reference(p_keycode);
	event->set_unicode(p_unicode);
	event->set_key_label(fix_key_label(p_unicode, p_keycode & KeyModifierMask::CODE_MASK));
	event->set_pressed(p_pressed);
	event->set_window_id(p_window_id);
	Input::get_singleton()->parse_input_event(event);
}

bool parse_keycode(const String &p_text, Key &r_keycode) {
	const PackedStringArray parts = p_text.split("+", false);
	if (parts.is_empty()) {
		return false;
	}

	const String key_text = parts[parts.size() - 1].strip_edges();
	Key keycode = find_keycode(key_text);
	if (keycode == Key::NONE && key_text.length() == 1) {
		keycode = fix_keycode(key_text[0], static_cast<Key>(String::char_uppercase(key_text[0])));
	}
	if (keycode == Key::NONE) {
		return false;
	}

	for (int i = 0; i < parts.size() - 1; i++) {
		const String modifier = parts[i].strip_edges().to_lower();
		if (modifier == "shift") {
			keycode |= KeyModifierMask::SHIFT;
		} else if (modifier == "ctrl" || modifier == "control") {
			keycode |= KeyModifierMask::CTRL;
		} else if (modifier == "alt" || modifier == "option") {
			keycode |= KeyModifierMask::ALT;
		} else if (modifier == "meta" || modifier == "command" || modifier == "cmd" || modifier == "windows" || modifier == "win") {
			keycode |= KeyModifierMask::META;
		} else if (modifier == "cmdorctrl" || modifier == "commandorcontrol") {
			keycode |= KeyModifierMask::CMD_OR_CTRL;
		} else {
			return false;
		}
	}

	r_keycode = keycode;
	return true;
}

Dictionary type_text(const Dictionary &p_options) {
	const String text = p_options.get("text", String());
	Window *window = find_keyboard_window();
	if (window == nullptr) {
		return make_error("type", "window_unavailable", "The running game has no window available for keyboard input.");
	}

	for (int i = 0; i < text.length(); i++) {
		const char32_t unicode = text[i];
		const Key keycode = fix_keycode(unicode, Key::NONE);
		send_key_event(keycode, unicode, true, window->get_window_id());
		send_key_event(keycode, unicode, false, window->get_window_id());
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "type";
	response["characters"] = text.length();
	return response;
}

Dictionary send_key(const Dictionary &p_options) {
	const String requested_key = p_options.get("key", String());
	Key keycode = Key::NONE;
	if (!parse_keycode(requested_key, keycode)) {
		return make_error("key", "invalid_key", "Unknown key or key combination: " + requested_key);
	}

	const String state = p_options.get("state", "tap");
	if (state != "tap" && state != "down" && state != "up") {
		return make_error("key", "invalid_key_state", "Key state must be tap, down, or up.");
	}
	Window *window = find_keyboard_window();
	if (window == nullptr) {
		return make_error("key", "window_unavailable", "The running game has no window available for keyboard input.");
	}

	if (state != "up") {
		send_key_event(keycode, 0, true, window->get_window_id());
	}
	if (state != "down") {
		send_key_event(keycode, 0, false, window->get_window_id());
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "key";
	response["key"] = keycode_get_string(keycode);
	response["state"] = state;
	return response;
}

Dictionary send_action(const Dictionary &p_options) {
	const StringName action = p_options.get("action", StringName());
	if (action.is_empty() || !InputMap::get_singleton()->has_action(action)) {
		return make_error("action", "action_not_found", "InputMap action was not found: " + String(action));
	}
	const String state = p_options.get("state", "tap");
	if (state != "tap" && state != "down" && state != "up") {
		return make_error("action", "invalid_action_state", "Action state must be tap, down, or up.");
	}
	const float strength = p_options.get("strength", 1.0);
	if (!Math::is_finite(strength) || strength < 0.0f || strength > 1.0f) {
		return make_error("action", "invalid_action_strength", "Action strength must be from 0 to 1.");
	}

	if (state != "up") {
		Ref<InputEventAction> press;
		press.instantiate();
		press->set_action(action);
		press->set_strength(strength);
		press->set_pressed(true);
		Input::get_singleton()->parse_input_event(press);
	}
	if (state != "down") {
		Ref<InputEventAction> release;
		release.instantiate();
		release->set_action(action);
		release->set_strength(0.0f);
		release->set_pressed(false);
		Input::get_singleton()->parse_input_event(release);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "action";
	response["action"] = String(action);
	response["state"] = state;
	response["strength"] = strength;
	return response;
}

MouseButton get_mouse_button(const String &p_button) {
	if (p_button == "right") {
		return MouseButton::RIGHT;
	}
	if (p_button == "middle") {
		return MouseButton::MIDDLE;
	}
	return MouseButton::LEFT;
}

void send_mouse_button_event(const Vector2 &p_position, MouseButton p_button, bool p_pressed, bool p_double_click, int64_t p_window_id, BitField<MouseButtonMask> p_button_mask) {
	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_position(p_position);
	event->set_global_position(p_position);
	event->set_button_index(p_button);
	event->set_button_mask(p_button_mask);
	event->set_pressed(p_pressed);
	event->set_double_click(p_double_click);
	event->set_window_id(p_window_id);
	Input::get_singleton()->parse_input_event(event);
}

Dictionary click(const Dictionary &p_options) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Window *window = scene_tree ? scene_tree->get_root() : nullptr;
	if (window == nullptr) {
		return make_error("click", "window_unavailable", "The running game has no window available for mouse input.");
	}

	Vector2 position;
	String target_path;
	if (p_options.has("target")) {
		target_path = p_options.get("target", String());
		Node *node = window->get_node_or_null(NodePath(target_path));
		if (node == nullptr) {
			return make_error("click", "node_not_found", "Click target was not found: " + target_path);
		}
		Control *control = Object::cast_to<Control>(node);
		if (control == nullptr) {
			return make_error("click", "target_not_control", "Click target is not a Control: " + target_path);
		}
		if (!control->is_visible_in_tree()) {
			return make_error("click", "target_not_visible", "Click target is not visible: " + target_path);
		}
		if (control->get_mouse_filter_with_override() == Control::MOUSE_FILTER_IGNORE) {
			return make_error("click", "target_ignores_mouse", "Click target ignores mouse input: " + target_path);
		}
		if (control->get_size().x <= 0.0f || control->get_size().y <= 0.0f) {
			return make_error("click", "target_has_no_area", "Click target has no clickable area: " + target_path);
		}
		position = control->get_screen_transform().xform(control->get_size() * 0.5f);
		window = control->get_window();
		if (window == nullptr) {
			return make_error("click", "window_unavailable", "Click target has no window: " + target_path);
		}
	} else {
		position.x = p_options.get("x", 0.0);
		position.y = p_options.get("y", 0.0);
		if (!Math::is_finite(position.x) || !Math::is_finite(position.y)) {
			return make_error("click", "invalid_position", "Click coordinates must be finite numbers.");
		}
	}

	const String button_name = p_options.get("button", "left");
	if (button_name != "left" && button_name != "right" && button_name != "middle") {
		return make_error("click", "invalid_mouse_button", "Mouse button must be left, right, or middle.");
	}
	const MouseButton button = get_mouse_button(button_name);
	const MouseButtonMask button_flag = mouse_button_to_mask(button);
	const BitField<MouseButtonMask> initial_mask = Input::get_singleton()->get_mouse_button_mask();
	BitField<MouseButtonMask> pressed_mask = initial_mask;
	pressed_mask.set_flag(button_flag);
	BitField<MouseButtonMask> released_mask = initial_mask;
	released_mask.clear_flag(button_flag);

	Ref<InputEventMouseMotion> motion;
	motion.instantiate();
	motion->set_position(position);
	motion->set_global_position(position);
	motion->set_relative(position - Input::get_singleton()->get_mouse_position());
	motion->set_relative_screen_position(motion->get_relative());
	motion->set_button_mask(initial_mask);
	motion->set_window_id(window->get_window_id());
	Input::get_singleton()->parse_input_event(motion);

	send_mouse_button_event(position, button, true, false, window->get_window_id(), pressed_mask);
	send_mouse_button_event(position, button, false, false, window->get_window_id(), released_mask);
	if ((bool)p_options.get("double", false)) {
		send_mouse_button_event(position, button, true, true, window->get_window_id(), pressed_mask);
		send_mouse_button_event(position, button, false, true, window->get_window_id(), released_mask);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "click";
	response["target"] = target_path;
	response["x"] = position.x;
	response["y"] = position.y;
	response["button"] = button_name;
	response["double"] = (bool)p_options.get("double", false);
	return response;
}

Error parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured) {
	r_captured = p_message == "request";
	if (!r_captured) {
		return OK;
	}
	if (p_arguments.size() != 3 || p_arguments[0].get_type() != Variant::INT || p_arguments[1].get_type() != Variant::STRING || p_arguments[2].get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}

	const uint64_t request_id = p_arguments[0];
	const String command = p_arguments[1];
	const Dictionary options = p_arguments[2];
	Dictionary response;
	if (command == "tree") {
		Dictionary tree_error;
		const Array tree = collect_tree(options, tree_error);
		if (!tree_error.is_empty()) {
			response = tree_error;
		} else {
			response["ok"] = true;
			response["command"] = "tree";
			response["tree"] = tree;
		}
	} else if (command == "ss") {
		const Dictionary screenshot = take_screenshot(options);
		if (screenshot.has("ok") && !(bool)screenshot["ok"]) {
			response = screenshot;
		} else {
			response["ok"] = true;
			response["command"] = "ss";
			response["screenshot"] = screenshot;
		}
	} else if (command == "observe") {
		Dictionary tree_error;
		const Array tree = collect_tree(options, tree_error);
		if (!tree_error.is_empty()) {
			response = tree_error;
			response["command"] = "observe";
		} else {
			const Dictionary screenshot = take_screenshot(options);
			if (screenshot.has("ok") && !(bool)screenshot["ok"]) {
				response = screenshot;
				response["command"] = "observe";
			} else {
				response["ok"] = true;
				response["command"] = "observe";
				response["tree"] = tree;
				response["screenshot"] = screenshot;
			}
		}
	} else if (command == "click") {
		response = click(options);
	} else if (command == "type") {
		response = type_text(options);
	} else if (command == "key") {
		response = send_key(options);
	} else if (command == "action") {
		response = send_action(options);
	} else {
		response = make_error(command, "unknown_game_command", "Unknown WGodot game command: " + command);
	}

	EngineDebugger::get_singleton()->send_message("wgodot:response", { request_id, response });
	return OK;
}

} // namespace

#endif // DEBUG_ENABLED

void initialize() {
#ifdef DEBUG_ENABLED
	if (EngineDebugger::is_active() && !EngineDebugger::has_capture(SNAME("wgodot"))) {
		EngineDebugger::register_message_capture(SNAME("wgodot"), EngineDebugger::Capture(nullptr, parse_message));
		capture_registered = true;
	}
#endif
}

void deinitialize() {
#ifdef DEBUG_ENABLED
	if (capture_registered && EngineDebugger::has_capture(SNAME("wgodot"))) {
		EngineDebugger::unregister_message_capture(SNAME("wgodot"));
	}
	capture_registered = false;
#endif
}

} // namespace WGodotGameBridge
