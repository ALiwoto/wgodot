// wgodot-changes::file
/**************************************************************************/
/*  wgodot_wait_controller.cpp                                            */
/**************************************************************************/

#include "wgodot_wait_controller.h"

#include "wgodot_pause_controller.h"

#include "core/debugger/engine_debugger.h"
#include "scene/main/scene_tree.h"

namespace WGodotWaitController {

namespace {

struct PendingWaitRequest {
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

PendingWaitRequest process_wait_request;
PendingWaitRequest physics_wait_request;

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = "wait";
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

void send_response(uint64_t p_request_id, const Dictionary &p_response) {
	if (p_request_id != 0 && EngineDebugger::is_active()) {
		EngineDebugger::get_singleton()->send_message("wgodot:response", { p_request_id, p_response });
	}
}

void complete_wait(PendingWaitRequest &r_request, const String &p_phase) {
	const uint64_t request_id = r_request.request_id;
	const int requested_count = r_request.requested_count;
	r_request.clear();

	Dictionary response;
	response["ok"] = true;
	response["command"] = "wait";
	response["phase"] = p_phase;
	response["count"] = requested_count;
	send_response(request_id, response);
}

void cancel_wait(PendingWaitRequest &r_request, const String &p_message) {
	if (!r_request.is_pending()) {
		return;
	}
	const uint64_t request_id = r_request.request_id;
	r_request.clear();
	send_response(request_id, make_error("wait_canceled", p_message));
}

} // namespace

void initialize() {
	process_wait_request.clear();
	physics_wait_request.clear();
}

void deinitialize() {
	process_wait_request.clear();
	physics_wait_request.clear();
}

bool request_wait(uint64_t p_request_id, int p_count, bool p_physics, Dictionary &r_error) {
	if (p_count <= 0) {
		r_error = make_error("invalid_wait_count", "Wait count must be a positive integer.");
		return false;
	}
	if (p_physics && WGodotPauseController::is_physics_effectively_paused()) {
		r_error = make_error("physics_paused", "Physics is paused. Use step_physics to advance paused physics ticks.");
		return false;
	}
	if (!p_physics && WGodotPauseController::is_game_paused()) {
		r_error = make_error("game_paused", "The game is paused. Use step to advance paused process frames.");
		return false;
	}
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree == nullptr) {
		r_error = make_error("scene_tree_unavailable", "The running game has no scene tree.");
		return false;
	}
	if (scene_tree->is_suspended()) {
		r_error = make_error("game_suspended", "Wait cannot observe frames while the SceneTree is suspended.");
		return false;
	}
	if (scene_tree->is_paused()) {
		r_error = make_error("scene_tree_paused", "Wait cannot observe normal frames while the SceneTree is paused outside WGodot.");
		return false;
	}

	PendingWaitRequest &request = p_physics ? physics_wait_request : process_wait_request;
	if (request.is_pending()) {
		const String phase = p_physics ? "physics" : "process";
		r_error = make_error("wait_in_progress", vformat("Another %s wait request is already running.", phase));
		return false;
	}
	request.request_id = p_request_id;
	request.requested_count = p_count;
	request.remaining_count = p_count;
	return true;
}

void cancel_process_wait(const String &p_message) {
	cancel_wait(process_wait_request, p_message);
}

void cancel_physics_wait(const String &p_message) {
	cancel_wait(physics_wait_request, p_message);
}

void end_physics_step() {
	SceneTree *scene_tree = SceneTree::get_singleton();
	const bool scene_tree_blocks_wait = scene_tree && (scene_tree->is_paused() || scene_tree->is_suspended());
	if (!scene_tree_blocks_wait && physics_wait_request.is_pending() && physics_wait_request.remaining_count > 0) {
		physics_wait_request.remaining_count--;
	}
}

void end_frame() {
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (scene_tree && (scene_tree->is_paused() || scene_tree->is_suspended())) {
		cancel_process_wait("The SceneTree was paused or suspended before the requested process frames completed.");
		cancel_physics_wait("The SceneTree was paused or suspended before the requested physics ticks completed.");
		return;
	}
	if (process_wait_request.is_pending()) {
		process_wait_request.remaining_count--;
		if (process_wait_request.remaining_count == 0) {
			complete_wait(process_wait_request, "process");
		}
	}
	if (physics_wait_request.is_pending() && physics_wait_request.remaining_count == 0) {
		complete_wait(physics_wait_request, "physics");
	}
}

} // namespace WGodotWaitController
