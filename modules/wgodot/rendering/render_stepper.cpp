// wgodot-changes::file
#include "render_stepper.h"

#ifdef TOOLS_ENABLED
#include "gpu_profile.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

WRenderStepper *WRenderStepper::active = nullptr;

void WRenderStepper::_bind_methods() {
	ClassDB::bind_static_method(get_class_static(), D_METHOD("begin"), &WRenderStepper::begin);
	ClassDB::bind_method(D_METHOD("request_frame", "step"), &WRenderStepper::request_frame);
	ClassDB::bind_method(D_METHOD("finish"), &WRenderStepper::finish);
}

Ref<WRenderStepper> WRenderStepper::begin() {
	ERR_FAIL_COND_V_MSG(!Thread::is_main_thread(), Ref<WRenderStepper>(), "Render stepping must begin on the main thread.");
	ERR_FAIL_COND_V_MSG(active, Ref<WRenderStepper>(), "Another render stepper already owns the render loop.");
	Ref<WRenderStepper> result;
	result.instantiate();
	active = result.ptr();
	return result;
}

Error WRenderStepper::request_frame(double p_step) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V_MSG(requested, ERR_BUSY, "The previous requested frame has not been submitted.");
	ERR_FAIL_COND_V(!Math::is_finite(p_step) || p_step < 0, ERR_INVALID_PARAMETER);
	frame_step = p_step;
	requested = true;
	return OK;
}

void WRenderStepper::finish() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (active == this) {
		active = nullptr;
	}
	requested = false;
}

void WRenderStepper::prepare_iteration() {
	if (!active || !active->draw_pending) {
		return;
	}
	Ref<WRenderStepper> stepper(active);
	WGodotGPUProfile::Scope profile_scope("capture.wait_previous_render");
	// Normal gameplay overlaps processing with the previous render. Offline
	// sampling instead needs that render's completed state before advancing.
	// Waiting only after scene processing makes a ready sample miss its turn
	// in the work queue and sleep for another whole capped frame interval.
	RenderingServer::get_singleton()->sync();
	stepper->draw_pending = false;
}

void WRenderStepper::_retire_resources() {
	WGodotGPUProfile::Scope profile_scope("render.resource_retirement");
	// Advance the device's deferred-deletion ring without updating particles,
	// rendering viewports, advancing visual history or emitting draw signals.
	RenderingDevice::get_singleton()->swap_buffers(false);
}

bool WRenderStepper::iterate(bool p_pending_resources) {
	if (!active) {
		return false;
	}
	// frame_pre_draw callbacks can release the caller's reference.
	Ref<WRenderStepper> stepper(active);
	if (stepper->requested) {
		stepper->requested = false;
		stepper->draw_pending = true;
		RenderingServer::get_singleton()->draw(true, stepper->frame_step);
		Engine::get_singleton()->increment_frames_drawn();
	} else if (p_pending_resources) {
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&WRenderStepper::_retire_resources));
	}
	return true;
}

WRenderStepper::~WRenderStepper() {
	if (active == this) {
		active = nullptr;
	}
}
#endif
