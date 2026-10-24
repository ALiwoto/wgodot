// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"

// Offline sampling owns displayed frames, while SceneTree and worker queues
// continue processing. Releasing the scope restores ordinary rendering.
class WRenderStepper : public RefCounted {
	GDCLASS(WRenderStepper, RefCounted);
	static WRenderStepper *active;
	bool requested = false;
	bool draw_pending = false;
	double frame_step = 0;
	static void _retire_resources();

protected:
	static void _bind_methods();

public:
	static Ref<WRenderStepper> begin();
	Error request_frame(double p_step);
	void finish();
	static void prepare_iteration();
	static bool iterate(bool p_pending_resources);
	~WRenderStepper();
};
#endif
