// wgodot-changes::file
#pragma once

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/planar_reflection_filter.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

class PlanarReflectionFilter {
	PlanarReflectionFilterShaderRD shader;
	RID version;
	PipelineCacheRD pipeline;

public:
	PlanarReflectionFilter();
	~PlanarReflectionFilter();
	void render(const Ref<RenderSceneBuffersRD> &p_buffers, const RenderSceneDataRD &p_scene, RID p_target);
};

} // namespace RendererRD
