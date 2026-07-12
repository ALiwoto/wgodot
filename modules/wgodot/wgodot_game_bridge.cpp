// wgodot-changes::file
/**************************************************************************/
/*  wgodot_game_bridge.cpp                                                */
/**************************************************************************/

#include "wgodot_game_bridge.h"

#include "core/debugger/engine_debugger.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/vector.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

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
			entry["type"] = String(node->get_class());
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
