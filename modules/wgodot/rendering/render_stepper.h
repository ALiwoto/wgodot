// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"

// Offline sampling owns displayed frames, while SceneTree and worker queues
// continue processing. Releasing the scope restores ordinary rendering.
class WRenderStepper : public RefCounted {
	GDCLASS(WRenderStepper, RefCounted);
	static WRenderStepper *active;
	// Render-thread state; writes are ordered with draw submission.
	static double shader_time_override;
	static void _set_shader_time(double p_time);
	bool requested = false;
	bool draw_pending = false;
	bool processing_requested = false;
	double frame_step = 0;
	static void _retire_resources();

protected:
	static void _bind_methods();

public:
	static Ref<WRenderStepper> begin();
	Error request_frame(double p_step);
	void set_shader_time(double p_time);
	static double shader_time(double p_default_time);
	// Ready CPU preparation can continue without spending another displayed
	// frame interval. Waiting for I/O must not request immediate processing.
	void request_processing();
	void finish();
	static void prepare_iteration();
	static bool iterate(bool p_pending_resources);
	static bool should_delay_iteration();
	static bool is_active() { return active != nullptr; }
	~WRenderStepper();
};
#endif
