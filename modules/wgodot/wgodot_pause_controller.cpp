// wgodot-changes::file
/**************************************************************************/
/*  wgodot_pause_controller.cpp                                           */
/**************************************************************************/

#include "wgodot_pause_controller.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/object.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#endif

#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#endif

namespace WGodotPauseController {

namespace {

struct PendingStepRequest {
	uint64_t request_id = 0;
	int requested_count = 0;
	int remaining_count = 0;

	bool is_pending() const {
		return request_id != 0;
	}

	void clear() {
		request_id = 0;
		requested_count = 0;
		remaining_count = 0;
	}
};

bool game_paused = false;
bool physics_paused = false;
bool scene_tree_was_paused = false;
ObjectID resumed_subtree_root_id;
bool process_step_active = false;
bool physics_step_active = false;
PendingStepRequest process_step_request;
PendingStepRequest physics_step_request;

Dictionary make_error(const String &p_command, const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = p_command;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

void set_physics_servers_active(bool p_active) {
#ifndef PHYSICS_3D_DISABLED
	PhysicsServer3D::get_singleton()->set_active(p_active);
#endif
#ifndef PHYSICS_2D_DISABLED
	PhysicsServer2D::get_singleton()->set_active(p_active);
#endif
}

void update_physics_server_state() {
	SceneTree *scene_tree = SceneTree::get_singleton();
	const bool scene_tree_blocks_physics = scene_tree && (scene_tree->is_paused() || scene_tree->is_suspended());
	set_physics_servers_active(!is_physics_effectively_paused() && !scene_tree_blocks_physics);
}

void send_response(uint64_t p_request_id, const Dictionary &p_response) {
	if (p_request_id != 0 && EngineDebugger::is_active()) {
		EngineDebugger::get_singleton()->send_message("wgodot:response", { p_request_id, p_response });
	}
}

void complete_step_request(PendingStepRequest &r_request, const String &p_command) {
	const uint64_t request_id = r_request.request_id;
	const int requested_count = r_request.requested_count;
	r_request.clear();

	Dictionary response = get_state();
	response["ok"] = true;
	response["command"] = p_command;
	response["count"] = requested_count;
	send_response(request_id, response);
}

void cancel_step_request(PendingStepRequest &r_request, const String &p_command, const String &p_message) {
	if (!r_request.is_pending()) {
		return;
	}
	const uint64_t request_id = r_request.request_id;
	r_request.clear();
	send_response(request_id, make_error(p_command, "step_canceled", p_message));
}

Node *get_resumed_subtree_root() {
	if (!resumed_subtree_root_id.is_valid()) {
		return nullptr;
	}
	Node *root = ObjectDB::get_instance<Node>(resumed_subtree_root_id);
	if (root == nullptr || !root->is_inside_tree()) {
		resumed_subtree_root_id = ObjectID();
		return nullptr;
	}
	return root;
}

void clear_resumed_subtree() {
	resumed_subtree_root_id = ObjectID();
}

} // namespace

void initialize() {
	game_paused = false;
	physics_paused = false;
	scene_tree_was_paused = false;
	clear_resumed_subtree();
	process_step_active = false;
	physics_step_active = false;
	process_step_request.clear();
	physics_step_request.clear();
}

void deinitialize() {
	process_step_request.clear();
	physics_step_request.clear();
	process_step_active = false;
	physics_step_active = false;
	game_paused = false;
	physics_paused = false;
	clear_resumed_subtree();
}

void set_game_paused(bool p_paused) {
	if (game_paused == p_paused) {
		if (p_paused) {
			clear_resumed_subtree();
		}
		return;
	}

	SceneTree *scene_tree = SceneTree::get_singleton();
	if (p_paused) {
		game_paused = true;
		scene_tree_was_paused = scene_tree && scene_tree->is_paused();
		if (scene_tree && !scene_tree_was_paused) {
			scene_tree->set_pause(true);
		}
	} else {
		cancel_step_request(process_step_request, "step", "The game was resumed before the requested process steps completed.");
		game_paused = false;
		clear_resumed_subtree();
		if (scene_tree && !scene_tree_was_paused && scene_tree->is_paused()) {
			scene_tree->set_pause(false);
		}
		scene_tree_was_paused = false;
		if (!is_physics_effectively_paused()) {
			cancel_step_request(physics_step_request, "step_physics", "Physics was resumed before the requested steps completed.");
		}
	}
	update_physics_server_state();
}

bool is_game_paused() {
	return game_paused;
}

bool resume_subtree(Node *p_root, Dictionary &r_error) {
	if (!game_paused) {
		r_error = make_error("resume", "game_not_paused", "Pause the game before resuming a node subtree.");
		return false;
	}
	if (p_root == nullptr || !p_root->is_inside_tree()) {
		r_error = make_error("resume", "node_not_in_tree", "The node subtree root must be inside the running SceneTree.");
		return false;
	}
	resumed_subtree_root_id = p_root->get_instance_id();
	return true;
}

bool has_resumed_subtree() {
	return game_paused && get_resumed_subtree_root() != nullptr;
}

bool is_node_in_resumed_subtree(const Node *p_node) {
	if (!game_paused || p_node == nullptr) {
		return false;
	}
	Node *root = get_resumed_subtree_root();
	return root != nullptr && (root == p_node || root->is_ancestor_of(p_node));
}

void set_physics_paused(bool p_paused) {
	if (physics_paused == p_paused) {
		return;
	}
	physics_paused = p_paused;
	if (!is_physics_effectively_paused()) {
		cancel_step_request(physics_step_request, "step_physics", "Physics was resumed before the requested steps completed.");
	}
	update_physics_server_state();
}

bool is_physics_paused() {
	return physics_paused;
}

bool is_physics_effectively_paused() {
	return game_paused || physics_paused;
}

bool request_process_steps(uint64_t p_request_id, int p_count, Dictionary &r_error) {
	if (!game_paused) {
		r_error = make_error("step", "game_not_paused", "Pause the game before using step.");
		return false;
	}
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree && scene_tree->is_suspended()) {
		r_error = make_error("step", "game_suspended", "Process steps cannot run while the SceneTree is suspended.");
		return false;
	}
	if (process_step_request.is_pending()) {
		r_error = make_error("step", "step_in_progress", "Another process step request is already running.");
		return false;
	}
	process_step_request.request_id = p_request_id;
	process_step_request.requested_count = p_count;
	process_step_request.remaining_count = p_count;
	return true;
}

bool request_physics_steps(uint64_t p_request_id, int p_count, Dictionary &r_error) {
	if (!is_physics_effectively_paused()) {
		r_error = make_error("step_physics", "physics_not_paused", "Pause the game or physics before using step_physics.");
		return false;
	}
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree && scene_tree->is_suspended()) {
		r_error = make_error("step_physics", "game_suspended", "Physics steps cannot run while the SceneTree is suspended.");
		return false;
	}
	if (physics_step_request.is_pending()) {
		r_error = make_error("step_physics", "step_in_progress", "Another physics step request is already running.");
		return false;
	}
	physics_step_request.request_id = p_request_id;
	physics_step_request.requested_count = p_count;
	physics_step_request.remaining_count = p_count;
	return true;
}

bool begin_process_frame() {
	process_step_active = false;
	if (!game_paused) {
		return true;
	}
	if (process_step_request.is_pending()) {
		process_step_active = true;
		return true;
	}
	return has_resumed_subtree();
}

void end_process_frame() {
	if (!process_step_active) {
		return;
	}
	process_step_active = false;
	process_step_request.remaining_count--;
	if (process_step_request.remaining_count == 0) {
		complete_step_request(process_step_request, "step");
	}
}

bool begin_physics_step() {
	physics_step_active = false;
	if (!is_physics_effectively_paused()) {
		return true;
	}
	if (!physics_step_request.is_pending()) {
		return false;
	}
	physics_step_active = true;
	set_physics_servers_active(true);
	return true;
}

void end_physics_step() {
	if (!physics_step_active) {
		return;
	}
	physics_step_active = false;
	update_physics_server_state();
	physics_step_request.remaining_count--;
	if (physics_step_request.remaining_count == 0) {
		complete_step_request(physics_step_request, "step_physics");
	}
}

bool is_process_step_active() {
	return process_step_active;
}

bool is_physics_step_active() {
	return physics_step_active;
}

Dictionary get_state() {
	Dictionary state;
	state["paused"] = game_paused;
	state["physics_paused"] = physics_paused;
	state["physics_effectively_paused"] = is_physics_effectively_paused();
	state["process_step_pending"] = process_step_request.is_pending();
	state["physics_step_pending"] = physics_step_request.is_pending();
	Node *resumed_subtree_root = get_resumed_subtree_root();
	state["resumed_subtree"] = resumed_subtree_root != nullptr ? String(resumed_subtree_root->get_path()) : String();
	return state;
}

} // namespace WGodotPauseController
