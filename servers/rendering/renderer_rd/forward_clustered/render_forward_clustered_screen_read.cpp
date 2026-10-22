// wgodot-changes::file

#include "render_forward_clustered.h"

namespace RendererSceneRenderImplementation {

void RenderForwardClustered::_render_list_with_current_screen(RenderListParameters *p_params, RID p_framebuffer, const RenderDataRD *p_render_data, RID p_current_uniform_set) {
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	const bool use_msaa = rb->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;
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

		// Close the preceding render pass before resolving/copying. RD tracks
		// these dependencies; the framebuffer is never sampled while attached.
		// Keep its MSAA color/depth intact for subsequent transparent draws.
		if (use_msaa) {
			for (uint32_t view = 0; view < rb->get_view_count(); view++) {
				RD::get_singleton()->texture_resolve_multisample(rb->get_color_msaa(view), rb->get_internal_texture(view));
			}
		}
		_render_buffers_copy_screen_texture(p_render_data, destination, surface->shader->uses_screen_texture_mipmaps);
		draw_range(i, 1, p_current_uniform_set);
		first = i + 1;
	}
	draw_range(first, p_params->element_count - first, p_params->render_pass_uniform_set);
}

} // namespace RendererSceneRenderImplementation
