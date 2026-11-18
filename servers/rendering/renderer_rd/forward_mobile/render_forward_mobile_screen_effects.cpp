// wgodot-changes::file
#include "render_forward_mobile.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/utilities.h"

namespace RendererSceneRenderImplementation {

void RenderForwardMobile::_render_list_with_current_screen(RenderListParameters *p_params, RID p_framebuffer, const RenderDataRD *p_render_data, RID p_current_uniform_set) {
	const StringName destination = SNAME("screen_read_current");
	auto draw_range = [&](int p_start, int p_count, RID p_uniform_set) {
		if (p_count == 0) {
			return;
		}
		RenderListParameters range = *p_params;
		range.elements += p_start;
		range.element_info += p_start;
		range.element_count = p_count;
		range.element_offset += p_start;
		range.render_pass_uniform_set = p_uniform_set;
		_render_list_with_draw_list(&range, p_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, 0u, p_render_data->render_region);
	};

	int first = 0;
	for (int i = 0; i < p_params->element_count; i++) {
		const GeometryInstanceSurfaceDataCache *surface = p_params->elements[i];
		if (!surface->shader->screen_read_current || surface->owner->instance_count == 0) {
			continue;
		}
		draw_range(first, i - first, p_params->render_pass_uniform_set);
		// Mobile color framebuffers resolve MSAA when their render pass ends.
		// Late effects already draw to resolved color, so never resolve it again.
		_render_buffers_copy_screen_texture(p_render_data, destination, surface->shader->uses_screen_texture_mipmaps);
		draw_range(i, 1, p_current_uniform_set);
		first = i + 1;
	}
	draw_range(first, p_params->element_count - first, p_params->render_pass_uniform_set);
}

void RenderForwardMobile::_render_screen_effects(const RenderDataRD *p_render_data, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, bool p_reverse_cull, SceneShaderForwardMobile::ShaderSpecialization p_specialization) {
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	const uint32_t views = rb->get_view_count();
	const bool multiview = views > 1;

	if (!render_list[RENDER_LIST_DISTORTION].elements.is_empty()) {
		RENDER_TIMESTAMP("Accumulate Screen Distortion");
		if (!screen_distortion) {
			screen_distortion = memnew(RendererRD::ScreenDistortion);
		}
		RID uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_DISTORTION, p_render_data, multiview, p_radiance_texture, p_samplers, true);
		RenderListParameters params(render_list[RENDER_LIST_DISTORTION].elements.ptr(), render_list[RENDER_LIST_DISTORTION].element_info.ptr(), render_list[RENDER_LIST_DISTORTION].elements.size(), p_reverse_cull, PASS_MODE_COLOR, uniform_set, p_specialization, false, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, views);
		_render_list_with_draw_list(&params, screen_distortion->framebuffer(rb), RD::DRAW_CLEAR_COLOR_0, { Color(0, 0, 0, 0) }, 0.0f, 0u, p_render_data->render_region);
		RENDER_TIMESTAMP("Composite Screen Distortion");
		screen_distortion->composite(rb, p_render_data->render_region);
	}

	if (!render_list[RENDER_LIST_SCREEN_EFFECT].elements.is_empty()) {
		RENDER_TIMESTAMP("Render Late Screen Effects");
		const StringName destination = SNAME("screen_read_current");
		_render_buffers_ensure_screen_texture(p_render_data, destination);
		RID uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SCREEN_EFFECT, p_render_data, multiview, p_radiance_texture, p_samplers, true, 0u, rb->get_texture(RB_SCOPE_BUFFERS, destination));
		RID framebuffer = FramebufferCacheRD::get_singleton()->get_cache_multiview(views, rb->get_internal_texture(), rb->get_depth_texture());
		RenderListParameters params(render_list[RENDER_LIST_SCREEN_EFFECT].elements.ptr(), render_list[RENDER_LIST_SCREEN_EFFECT].element_info.ptr(), render_list[RENDER_LIST_SCREEN_EFFECT].elements.size(), p_reverse_cull, PASS_MODE_COLOR, uniform_set, p_specialization, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, views, 0u, true);
		_render_list_with_current_screen(&params, framebuffer, p_render_data, uniform_set);
	}
}

} // namespace RendererSceneRenderImplementation
