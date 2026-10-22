// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/io/config_file.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/safe_refcount.h"

class RenderDataRD;

// Opt-in, lossless, pre-UI measurements of the actual renderer. No node-type
// enumeration: private particle instances and every submitted surface count.
class WRenderCapture : public RefCounted {
	GDCLASS(WRenderCapture, RefCounted);
	struct Plane {
		String name;
		int width = 0;
		int height = 0;
		int format = 0;
		PackedByteArray bytes;
		Error error = OK;
		bool buffer = false;
	};
	static HashMap<RID, Ref<WRenderCapture>> captures;
	RID viewport;
	RID target;
	String directory;
	bool passes = false;
	int64_t sample_frame = -1;
	double sample_seconds = 0;
	double sample_render_time = -1;
	double sample_time_step = 0;
	int frame_index = 0;
	Vector<Plane> planes;
	Ref<ConfigFile> input_data;
	HashSet<RID> input_buffers;
	bool detailed_inputs = false;
	SafeFlag completed;
	SafeNumeric<int> failure;

	void _begin();
	static void _begin_capture(Ref<WRenderCapture> p_capture);
	void _end();
	void _set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs);
	void _read_texture(const String &p_name, RID p_texture);
	void _write_plane(uint32_t p_index);
	static void _write_plane_task(void *p_userdata, uint32_t p_index);
	static WRenderCapture *_find(const RenderDataRD *p_data);

protected:
	static void _bind_methods();

public:
	static Ref<WRenderCapture> begin(RID p_viewport, const String &p_directory, bool p_passes = false);
	void set_sample(int64_t p_frame, double p_seconds, double p_render_time = -1, double p_time_step = 0, bool p_detailed_inputs = false);
	void end();
	bool is_completed() const { return completed.is_set(); }
	int get_error() const { return failure.get(); }
	static void stage(const RenderDataRD *p_data, const String &p_name, bool p_multisample = false);
	static void texture(const RenderDataRD *p_data, const String &p_name, RID p_texture);
	static Ref<ConfigFile> inputs(const RenderDataRD *p_data);
	static String buffer(const RenderDataRD *p_data, RID p_buffer);
	static bool prepare_frame(const RenderDataRD *p_data);
	static void finish_frame(const RenderDataRD *p_data);
};
#endif
