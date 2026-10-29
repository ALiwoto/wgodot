// wgodot-changes::file
#include "planar_reflection_filter.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

PlanarReflectionFilter::PlanarReflectionFilter() {
	shader.initialize({ String() });
	version = shader.version_create();
	pipeline.setup(shader.version_get_shader(version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), RD::PipelineColorBlendState::create_disabled(), 0);
}

PlanarReflectionFilter::~PlanarReflectionFilter() {
	pipeline.clear();
	shader.version_free(version);
}

void PlanarReflectionFilter::render(const Ref<RenderSceneBuffersRD> &p_buffers, const RenderSceneDataRD &p_scene, RID p_target) {
	struct PushConstants {
		float inverse_projection[16];
		float plane[4];
		float dimensions[4];
		float filter[4];
	} params = {};
	MaterialStorage::store_camera(p_scene.get_cam_projection().inverse(), params.inverse_projection);
	const Plane plane = p_scene.cam_transform.affine_inverse().xform(p_scene.clip_plane);
	params.plane[0] = plane.normal.x;
	params.plane[1] = plane.normal.y;
	params.plane[2] = plane.normal.z;
	params.plane[3] = -plane.d;
	const Size2i input_size = p_buffers->get_internal_size();
	const Size2i output_size = p_buffers->get_target_size();
	params.dimensions[0] = input_size.x;
	params.dimensions[1] = input_size.y;
	params.dimensions[2] = output_size.x;
	params.dimensions[3] = output_size.y;
	params.filter[0] = MIN(p_scene.reflection_roughness, 0.04f) * 0.5f * input_size.x;
	params.filter[1] = 1.0f / p_scene.reflection_roughness_distance;
	params.filter[2] = p_scene.cam_orthogonal ? 1.0f : 0.0f;
	RD *rd = RD::get_singleton();
	RID program = shader.version_get_shader(version, 0);
	RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID depth_sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform color(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, { sampler, p_buffers->get_internal_texture() });
	RD::Uniform depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, { depth_sampler, p_buffers->get_depth_texture() });
	RID uniforms = UniformSetCacheRD::get_singleton()->get_cache(program, 0, color, depth);
	RD::DrawListID list = rd->draw_list_begin(p_target, RD::DRAW_DEFAULT_ALL);
	rd->draw_list_bind_render_pipeline(list, pipeline.get_render_pipeline(RD::INVALID_ID, rd->framebuffer_get_format(p_target)));
	rd->draw_list_bind_uniform_set(list, uniforms, 0);
	rd->draw_list_set_push_constant(list, &params, sizeof(params));
	rd->draw_list_draw(list, false, 1, 3);
	rd->draw_list_end();
}
