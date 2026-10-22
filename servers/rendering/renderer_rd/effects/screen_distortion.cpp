// wgodot-changes::file
#include "screen_distortion.h"

#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

namespace {
constexpr RD::DataFormat OFFSET_FORMAT = RD::DATA_FORMAT_R16G16_SFLOAT;
constexpr uint32_t OFFSET_USAGE = RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
constexpr uint32_t OFFSET_MSAA_USAGE = RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
}

ScreenDistortion::ScreenDistortion() {
	shader.initialize({ "" });
	version = shader.version_create();
	pipeline.setup(shader.version_get_shader(version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), RD::PipelineColorBlendState::create_disabled(), 0);
}

ScreenDistortion::~ScreenDistortion() {
	pipeline.clear();
	shader.version_free(version);
}

RD::FramebufferFormatID ScreenDistortion::framebuffer_format(RD::TextureSamples p_samples, uint32_t p_views, bool p_storage) {
	bool msaa = p_samples != RD::TEXTURE_SAMPLES_1;
	RD::AttachmentFormat offsets;
	offsets.format = OFFSET_FORMAT;
	offsets.samples = p_samples;
	offsets.usage_flags = msaa ? OFFSET_MSAA_USAGE : OFFSET_USAGE;
	RD::AttachmentFormat depth;
	depth.format = RenderSceneBuffersRD::get_depth_format(false, msaa, p_storage);
	depth.samples = p_samples;
	depth.usage_flags = RenderSceneBuffersRD::get_depth_usage_bits(false, msaa, p_storage);
	return RD::get_singleton()->framebuffer_format_create({ offsets, depth }, p_views);
}

RID ScreenDistortion::framebuffer(const Ref<RenderSceneBuffersRD> &p_buffers) {
	const StringName scope = SNAME("ScreenDistortion");
	if (!p_buffers->has_texture(scope, SNAME("offsets"))) {
		p_buffers->create_texture(scope, SNAME("offsets"), OFFSET_FORMAT, OFFSET_USAGE);
	}
	RID offsets = p_buffers->get_texture(scope, SNAME("offsets"));
	RID depth = p_buffers->get_depth_texture();
	if (p_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED) {
		if (!p_buffers->has_texture(scope, SNAME("offsets_msaa"))) {
			p_buffers->create_texture(scope, SNAME("offsets_msaa"), OFFSET_FORMAT, OFFSET_MSAA_USAGE, p_buffers->get_texture_samples());
		}
		offsets = p_buffers->get_texture(scope, SNAME("offsets_msaa"));
		depth = p_buffers->get_depth_msaa();
	}
	return FramebufferCacheRD::get_singleton()->get_cache_multiview(p_buffers->get_view_count(), offsets, depth);
}

void ScreenDistortion::composite(const Ref<RenderSceneBuffersRD> &p_buffers, const Rect2 &p_region) {
	RD *rd = RD::get_singleton();
	const StringName scope = SNAME("ScreenDistortion");
	const Size2i size = p_buffers->get_internal_size();
	Rect2i region = p_region.has_area() ? Rect2i(p_region) : Rect2i(Point2i(), size);
	const int32_t bounds[4] = { region.position.x, region.position.y, region.get_end().x, region.get_end().y };
	if (!p_buffers->has_texture(scope, SNAME("composite"))) {
		p_buffers->create_texture(scope, SNAME("composite"), p_buffers->get_base_data_format(), RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT);
	}
	RID program = shader.version_get_shader(version, 0);
	RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	for (uint32_t view = 0; view < p_buffers->get_view_count(); view++) {
		RID offsets = p_buffers->get_texture_slice(scope, SNAME("offsets"), view, 0);
		if (p_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED) {
			rd->texture_resolve_multisample(p_buffers->get_texture_slice(scope, SNAME("offsets_msaa"), view, 0), offsets);
		}
		RID color = p_buffers->get_internal_texture(view);
		RID result = p_buffers->get_texture_slice(scope, SNAME("composite"), view, 0);
		RID target = FramebufferCacheRD::get_singleton()->get_cache(result);
		RD::Uniform source_uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, { sampler, color });
		RD::Uniform offsets_uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, { sampler, offsets });
		RID uniforms = UniformSetCacheRD::get_singleton()->get_cache(program, 0, source_uniform, offsets_uniform);
		RD::DrawListID list = rd->draw_list_begin(target, RD::DRAW_DEFAULT_ALL);
		rd->draw_list_bind_render_pipeline(list, pipeline.get_render_pipeline(RD::INVALID_ID, rd->framebuffer_get_format(target)));
		rd->draw_list_bind_uniform_set(list, uniforms, 0);
		rd->draw_list_set_push_constant(list, bounds, sizeof(bounds));
		rd->draw_list_draw(list, false, 1, 3);
		rd->draw_list_end();
		// Do not sample and write the same texture. Keep the render buffers'
		// canonical color attachment for subsequent compositor/AA/tonemap passes.
		CopyEffects::get_singleton()->copy_to_fb_rect(result, FramebufferCacheRD::get_singleton()->get_cache(color), Rect2i(Point2i(), size));
	}
}
