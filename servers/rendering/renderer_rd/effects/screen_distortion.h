// wgodot-changes::file
#pragma once

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_distortion.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

// Signed screen-UV offsets, accumulated independently of scene color. Buffers
// belong to the viewport and follow its normal resize/destruction lifetime.
class ScreenDistortion {
	ScreenDistortionShaderRD shader;
	RID version;
	PipelineCacheRD pipeline;

public:
	static RD::DataFormat offset_format();
	static String encoding_defines();
	ScreenDistortion();
	~ScreenDistortion();
	static RD::FramebufferFormatID framebuffer_format(RD::TextureSamples p_samples, uint32_t p_views, bool p_storage);
	RID framebuffer(const Ref<RenderSceneBuffersRD> &p_buffers);
	void composite(const Ref<RenderSceneBuffersRD> &p_buffers, const Rect2 &p_region);
};

} // namespace RendererRD
