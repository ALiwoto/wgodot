// wgodot-changes::file
/**************************************************************************/
/*  wgodot_game_bridge.cpp                                                */
/**************************************************************************/

#include "wgodot_game_bridge.h"

#include "wgodot_member_list.h"
#include "wgodot_pause_controller.h"
#include "wgodot_wait_controller.h"

#include "core/debugger/engine_debugger.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/vector.h"
#include "core/variant/variant_parser.h"
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

String get_property_display_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::STRING) {
		return "\"" + String(p_value).c_escape() + "\"";
	}
	return p_value.stringify();
}

bool parse_member_path(const String &p_member_path, Vector<StringName> &r_path) {
	r_path.clear();
	if (p_member_path.is_empty()) {
		return false;
	}

	const PackedStringArray segments = p_member_path.split(".");
	for (int i = 0; i < segments.size(); i++) {
		if (segments[i].is_empty()) {
			r_path.clear();
			return false;
		}
		r_path.push_back(segments[i]);
	}
	return true;
}

Variant get_nested_property(Object *p_object, const String &p_property_path, bool &r_valid) {
	r_valid = false;
	Vector<StringName> property_path;
	if (!parse_member_path(p_property_path, property_path)) {
		return Variant();
	}
	return p_object->get_indexed(property_path, &r_valid);
}

Node *get_target_node(const String &p_command, const Dictionary &p_options, Dictionary &r_error) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Node *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (root == nullptr) {
		r_error = make_error(p_command, "scene_tree_unavailable", "The running game has no scene tree.");
		return nullptr;
	}

	const String target_path = p_options.get("target", String());
	Node *target = target_path.is_empty() ? nullptr : root->get_node_or_null(NodePath(target_path));
	if (target == nullptr) {
		r_error = make_error(p_command, "node_not_found", "Target node was not found: " + target_path);
	}
	return target;
}

Dictionary make_value_info(const Variant &p_value) {
	String value_text;
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object();
		value_text = object ? vformat("<%s#%d>", object->get_class(), static_cast<int64_t>(object->get_instance_id())) : "null";
	} else if (p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::DICTIONARY) {
		value_text = p_value.stringify();
	} else if (VariantWriter::write_to_string(p_value, value_text) != OK) {
		value_text = p_value.stringify();
	}

	Dictionary info;
	info["type"] = Variant::get_type_name(p_value.get_type());
	info["value"] = value_text;
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object();
		if (object) {
			info["class"] = object->get_class();
			info["instance_id"] = static_cast<int64_t>(object->get_instance_id());
			Node *node = Object::cast_to<Node>(object);
			if (node && node->is_inside_tree()) {
				info["node_path"] = String(node->get_path());
			}
		}
	}
	return info;
}

Variant parse_cli_value(const String &p_source) {
	VariantParser::StreamString stream;
	stream.s = p_source;
	Variant value;
	String error_text;
	int error_line = 0;
	if (VariantParser::parse(&stream, value, error_text, error_line) == OK) {
		VariantParser::Token trailing_token;
		if (VariantParser::get_token(&stream, trailing_token, error_line, error_text) == OK && trailing_token.type == VariantParser::TK_EOF) {
			return value;
		}
	}
	return p_source;
}

bool resolve_named_class_member(
		const String &p_command, const String &p_qualified_path, String &r_class_name,
		String &r_member_path, Ref<Script> &r_script, Dictionary &r_error) {
	const int separator = p_qualified_path.find_char('.');
	if (separator <= 0 || separator == p_qualified_path.length() - 1) {
		r_error = make_error(p_command, "invalid_static_member_path", "Expected a named class and member path, such as GameStatics.current_value.");
		return false;
	}

	r_class_name = p_qualified_path.substr(0, separator);
	r_member_path = p_qualified_path.substr(separator + 1);
	Vector<StringName> member_path;
	if (!parse_member_path(r_member_path, member_path)) {
		r_error = make_error(p_command, "invalid_static_member_path", "Invalid static member path: " + p_qualified_path);
		return false;
	}
	if (!ScriptServer::is_global_class(r_class_name)) {
		r_error = make_error(p_command, "named_class_not_found", "Named script class was not found: " + r_class_name);
		return false;
	}

	const String script_path = ScriptServer::get_global_class_path(r_class_name);
	r_script = ResourceLoader::load(script_path, "Script");
	if (r_script.is_null() || !r_script->is_valid()) {
		r_error = make_error(p_command, "named_class_load_failed", "Could not load named script class " + r_class_name + " from: " + script_path);
		return false;
	}
	return true;
}

Dictionary call_object_method(
		const String &p_command, const String &p_target, Object *p_root,
		const String &p_method_path_text, const String &p_display_method,
		const PackedStringArray &p_argument_sources) {
	Vector<StringName> method_path;
	if (!parse_member_path(p_method_path_text, method_path)) {
		return make_error(p_command, "invalid_method_path", "Invalid method path: " + p_display_method);
	}

	const StringName method = method_path[method_path.size() - 1];
	Object *receiver = p_root;
	Variant receiver_holder;
	if (method_path.size() > 1) {
		method_path.resize(method_path.size() - 1);
		bool receiver_valid = false;
		receiver_holder = p_root->get_indexed(method_path, &receiver_valid);
		if (!receiver_valid) {
			return make_error(p_command, "method_receiver_not_found", "Method receiver was not found for: " + p_display_method);
		}
		if (receiver_holder.get_type() != Variant::OBJECT || receiver_holder.get_validated_object() == nullptr) {
			return make_error(p_command, "method_receiver_not_object", "Method receiver is not a live Object for: " + p_display_method);
		}
		receiver = receiver_holder.get_validated_object();
	}

	Vector<Variant> arguments;
	arguments.resize(p_argument_sources.size());
	for (int i = 0; i < p_argument_sources.size(); i++) {
		arguments.write[i] = parse_cli_value(p_argument_sources[i]);
	}
	Vector<const Variant *> argument_pointers;
	argument_pointers.resize(arguments.size());
	for (int i = 0; i < arguments.size(); i++) {
		argument_pointers.write[i] = &arguments[i];
	}

	Callable::CallError call_error;
	call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	const Variant result = receiver->callp(method, (const Variant **)argument_pointers.ptr(), argument_pointers.size(), call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		return make_error(p_command, "call_failed", Variant::get_call_error_text(receiver, method, (const Variant **)argument_pointers.ptr(), argument_pointers.size(), call_error));
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = p_command;
	response["target"] = p_target;
	response["method"] = p_display_method;
	response["result"] = make_value_info(result);
	return response;
}

Dictionary get_runtime_properties(const Dictionary &p_options) {
	Dictionary target_error;
	Node *target = get_target_node("get", p_options, target_error);
	if (target == nullptr) {
		return target_error;
	}

	const PackedStringArray properties = p_options.get("properties", PackedStringArray());
	if (properties.is_empty()) {
		return make_error("get", "property_required", "Get requires at least one property path.");
	}

	const ObjectID target_id = target->get_instance_id();
	const String target_path = String(target->get_path());
	Array values;
	for (const String &property : properties) {
		target = Object::cast_to<Node>(ObjectDB::get_instance(target_id));
		if (target == nullptr) {
			return make_error("get", "target_freed", "The target node was freed while reading properties: " + target_path);
		}
		bool valid = false;
		const Variant value = get_nested_property(target, property, valid);
		if (!valid) {
			return make_error("get", "property_not_found", "Property was not found: " + property);
		}
		Dictionary entry = make_value_info(value);
		entry["property"] = property;
		values.push_back(entry);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "get";
	response["target"] = target_path;
	response["values"] = values;
	return response;
}

Dictionary set_runtime_property(const Dictionary &p_options) {
	Dictionary target_error;
	Node *target = get_target_node("set", p_options, target_error);
	if (target == nullptr) {
		return target_error;
	}

	const String property = p_options.get("property", String());
	Vector<StringName> property_path;
	if (!parse_member_path(property, property_path)) {
		return make_error("set", "invalid_property_path", "Invalid property path: " + property);
	}

	const ObjectID target_id = target->get_instance_id();
	const String target_path = String(target->get_path());
	const String value_source = p_options.get("value", String());
	bool valid = false;
	target->set_indexed(property_path, parse_cli_value(value_source), &valid);
	if (!valid) {
		return make_error("set", "property_not_set", "Property could not be assigned: " + property);
	}
	target = Object::cast_to<Node>(ObjectDB::get_instance(target_id));
	if (target == nullptr) {
		return make_error("set", "target_freed", "The target node was freed while assigning: " + property);
	}

	const Variant actual_value = target->get_indexed(property_path, &valid);
	if (!valid) {
		return make_error("set", "property_not_found", "Property could not be read after assignment: " + property);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "set";
	response["target"] = target_path;
	response["property"] = property;
	response["result"] = make_value_info(actual_value);
	return response;
}

Dictionary call_runtime_method(const Dictionary &p_options) {
	Dictionary target_error;
	Node *target = get_target_node("call", p_options, target_error);
	if (target == nullptr) {
		return target_error;
	}

	const String target_path = String(target->get_path());
	const String method_path_text = p_options.get("method", String());
	const PackedStringArray argument_sources = p_options.get("arguments", PackedStringArray());
	return call_object_method("call", target_path, target, method_path_text, method_path_text, argument_sources);
}

Dictionary get_static_members(const Dictionary &p_options) {
	const PackedStringArray members = p_options.get("members", PackedStringArray());
	if (members.is_empty()) {
		return make_error("get_static", "static_member_required", "get_static requires at least one named-class member path.");
	}

	Array values;
	for (const String &qualified_path : members) {
		String class_name;
		String member_path_text;
		Ref<Script> script;
		Dictionary resolve_error;
		if (!resolve_named_class_member("get_static", qualified_path, class_name, member_path_text, script, resolve_error)) {
			return resolve_error;
		}

		bool valid = false;
		const Variant value = get_nested_property(script.ptr(), member_path_text, valid);
		if (!valid) {
			return make_error("get_static", "static_member_not_found", "Static member was not found: " + qualified_path);
		}
		Dictionary entry = make_value_info(value);
		entry["property"] = qualified_path;
		entry["declaring_class"] = class_name;
		values.push_back(entry);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "get_static";
	response["values"] = values;
	return response;
}

Dictionary set_static_member(const Dictionary &p_options) {
	const String qualified_path = p_options.get("member", String());
	String class_name;
	String member_path_text;
	Ref<Script> script;
	Dictionary resolve_error;
	if (!resolve_named_class_member("set_static", qualified_path, class_name, member_path_text, script, resolve_error)) {
		return resolve_error;
	}

	Vector<StringName> member_path;
	if (!parse_member_path(member_path_text, member_path)) {
		return make_error("set_static", "invalid_static_member_path", "Invalid static member path: " + qualified_path);
	}
	const String value_source = p_options.get("value", String());
	bool valid = false;
	script->set_indexed(member_path, parse_cli_value(value_source), &valid);
	if (!valid) {
		return make_error("set_static", "static_member_not_set", "Static member could not be assigned: " + qualified_path);
	}

	const Variant actual_value = script->get_indexed(member_path, &valid);
	if (!valid) {
		return make_error("set_static", "static_member_not_found", "Static member could not be read after assignment: " + qualified_path);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "set_static";
	response["target"] = class_name;
	response["property"] = qualified_path;
	response["result"] = make_value_info(actual_value);
	return response;
}

Dictionary call_static_method(const Dictionary &p_options) {
	const String qualified_path = p_options.get("method", String());
	String class_name;
	String method_path_text;
	Ref<Script> script;
	Dictionary resolve_error;
	if (!resolve_named_class_member("call_static", qualified_path, class_name, method_path_text, script, resolve_error)) {
		return resolve_error;
	}

	const PackedStringArray argument_sources = p_options.get("arguments", PackedStringArray());
	return call_object_method("call_static", class_name, script.ptr(), method_path_text, qualified_path, argument_sources);
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
	const PackedStringArray requested_properties = p_options.get("properties", PackedStringArray());
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
			if (!requested_properties.is_empty()) {
				Array properties;
				for (int i = 0; i < requested_properties.size(); i++) {
					const String property_name = requested_properties[i];
					bool valid = false;
					const Variant value = get_nested_property(node, property_name, valid);
					Dictionary property;
					property["name"] = property_name;
					property["valid"] = valid;
					property["value"] = valid ? get_property_display_value(value) : String("<missing>");
					properties.push_back(property);
				}
				entry["properties"] = properties;
			}
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
			if (!node->has_method(SNAME("simulate_click"))) {
				return make_error("click", "target_not_clickable", "Click target is not a Control and does not implement simulate_click(): " + target_path);
			}
			Callable::CallError call_error;
			node->callp(SNAME("simulate_click"), nullptr, 0, call_error);
			if (call_error.error != Callable::CallError::CALL_OK) {
				return make_error("click", "simulate_click_failed", "simulate_click() could not be called without arguments on: " + target_path);
			}

			Dictionary response;
			response["ok"] = true;
			response["command"] = "click";
			response["target"] = target_path;
			response["mode"] = "method";
			response["method"] = "simulate_click";
			return response;
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
	response["mode"] = "input";
	return response;
}

Dictionary make_pause_response(const String &p_command) {
	Dictionary response = WGodotPauseController::get_state();
	response["ok"] = true;
	response["command"] = p_command;
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
	bool response_deferred = false;
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
	} else if (command == "get") {
		response = get_runtime_properties(options);
	} else if (command == "set") {
		response = set_runtime_property(options);
	} else if (command == "call") {
		response = call_runtime_method(options);
	} else if (command == "get_static") {
		response = get_static_members(options);
	} else if (command == "set_static") {
		response = set_static_member(options);
	} else if (command == "call_static") {
		response = call_static_method(options);
	} else if (command == "list") {
		response = WGodotMemberList::execute(options);
	} else if (command == "wait") {
		const int count = options.get("count", 1);
		const bool physics = options.get("physics", false);
		Dictionary wait_error;
		if (WGodotWaitController::request_wait(request_id, count, physics, wait_error)) {
			response_deferred = true;
		} else {
			response = wait_error;
		}
	} else if (command == "pause") {
		WGodotWaitController::cancel_process_wait("The game was paused before the requested process frames completed.");
		WGodotWaitController::cancel_physics_wait("The game was paused before the requested physics ticks completed.");
		WGodotPauseController::set_game_paused(true);
		response = make_pause_response(command);
	} else if (command == "resume") {
		WGodotPauseController::set_game_paused(false);
		response = make_pause_response(command);
	} else if (command == "pause_physics") {
		WGodotWaitController::cancel_physics_wait("Physics was paused before the requested ticks completed.");
		WGodotPauseController::set_physics_paused(true);
		response = make_pause_response(command);
	} else if (command == "resume_physics") {
		WGodotPauseController::set_physics_paused(false);
		response = make_pause_response(command);
	} else if (command == "step" || command == "step_physics") {
		const int count = options.get("count", 1);
		if (count <= 0) {
			response = make_error(command, "invalid_step_count", "Step count must be a positive integer.");
		} else {
			Dictionary step_error;
			const bool accepted = command == "step" ? WGodotPauseController::request_process_steps(request_id, count, step_error) : WGodotPauseController::request_physics_steps(request_id, count, step_error);
			if (accepted) {
				response_deferred = true;
			} else {
				response = step_error;
			}
		}
	} else {
		response = make_error(command, "unknown_game_command", "Unknown WGodot game command: " + command);
	}

	if (!response_deferred) {
		EngineDebugger::get_singleton()->send_message("wgodot:response", { request_id, response });
	}
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
