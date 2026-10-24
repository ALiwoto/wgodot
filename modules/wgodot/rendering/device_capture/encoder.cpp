// wgodot-changes::file
#include "encoder.h"

#ifdef TOOLS_ENABLED
namespace WGPUCapture {
void Encoder::raw(const void *p_data, uint32_t p_size) {
	if (p_size == 0) {
		return;
	}
	const int64_t offset = bytes.size();
	bytes.resize(offset + p_size);
	memcpy(bytes.ptrw() + offset, p_data, p_size);
}
void Encoder::tag(Value p_type) { bytes.push_back(uint8_t(p_type)); }
void Encoder::u32(uint32_t p_value) {
	uint8_t data[4];
	encode_uint32(p_value, data);
	raw(data, sizeof(data));
}
void Encoder::u64(uint64_t p_value) {
	uint8_t data[8];
	encode_uint64(p_value, data);
	raw(data, sizeof(data));
}
void Encoder::put(bool p_value) {
	tag(Value::BOOL);
	bytes.push_back(p_value);
}
void Encoder::put(float p_value) {
	tag(Value::FLOAT);
	uint8_t data[4];
	encode_float(p_value, data);
	raw(data, sizeof(data));
}
void Encoder::put(double p_value) {
	tag(Value::DOUBLE);
	uint8_t data[8];
	encode_double(p_value, data);
	raw(data, sizeof(data));
}
void Encoder::put(RID p_value) {
	tag(Value::RID);
	u64(p_value.get_id());
}
void Encoder::put(const String &p_value) {
	tag(Value::STRING);
	const CharString text = p_value.utf8();
	u32(text.length());
	raw(text.get_data(), text.length());
}
void Encoder::put(Bytes p_value) {
	tag(Value::BYTES);
	u32(p_value.size);
	raw(p_value.data, p_value.size);
}
void Encoder::put(const RD::TextureFormat &v) {
	structure(Structure::TEXTURE_FORMAT, v.format, v.width, v.height, v.depth, v.array_layers, v.mipmaps, v.texture_type, v.samples, v.usage_bits, v.shareable_formats, v.is_resolve_buffer, v.is_discardable, v.is_subsampled);
}
void Encoder::put(const RD::TextureView &v) {
	structure(Structure::TEXTURE_VIEW, v.format_override, v.swizzle_r, v.swizzle_g, v.swizzle_b, v.swizzle_a);
}
void Encoder::put(const RD::SamplerState &v) {
	structure(Structure::SAMPLER, v.mag_filter, v.min_filter, v.mip_filter, v.repeat_u, v.repeat_v, v.repeat_w, v.lod_bias, v.use_anisotropy, v.anisotropy_max, v.enable_compare, v.compare_op, v.min_lod, v.max_lod, v.border_color, v.unnormalized_uvw);
}
void Encoder::put(const RD::VertexAttribute &v) {
	structure(Structure::VERTEX_ATTRIBUTE, v.binding, v.location, v.offset, v.format, v.stride, v.frequency);
}
void Encoder::put(const RD::FramebufferPass &v) {
	structure(Structure::FRAMEBUFFER_PASS, v.color_attachments, v.input_attachments, v.resolve_attachments, v.preserve_attachments, v.depth_attachment, v.depth_resolve_attachment);
}
void Encoder::put(const RD::AttachmentFormat &v) {
	structure(Structure::ATTACHMENT_FORMAT, v.format, v.samples, v.usage_flags);
}
void Encoder::put(const RD::Uniform &v) {
	Vector<RID> ids;
	ids.resize(v.get_id_count());
	for (int i = 0; i < ids.size(); i++) {
		ids.set(i, v.get_id(i));
	}
	structure(Structure::UNIFORM, v.uniform_type, v.binding, ids);
}
void Encoder::put(const RD::PipelineRasterizationState &v) {
	structure(Structure::RASTERIZATION, v.enable_depth_clamp, v.discard_primitives, v.wireframe, v.cull_mode, v.front_face, v.depth_bias_enabled, v.depth_bias_constant_factor, v.depth_bias_clamp, v.depth_bias_slope_factor, v.line_width, v.patch_control_points);
}
void Encoder::put(const RD::PipelineMultisampleState &v) {
	structure(Structure::MULTISAMPLE, v.sample_count, v.enable_sample_shading, v.min_sample_shading, v.sample_mask, v.enable_alpha_to_coverage, v.enable_alpha_to_one);
}
void Encoder::put(const RD::PipelineDepthStencilState &v) {
	structure(Structure::DEPTH_STENCIL, v.enable_depth_test, v.enable_depth_write, v.depth_compare_operator, v.enable_depth_range, v.depth_range_min, v.depth_range_max, v.enable_stencil, v.front_op, v.back_op);
}
void Encoder::put(const RD::PipelineDepthStencilState::StencilOperationState &v) {
	structure(Structure::STENCIL, v.fail, v.pass, v.depth_fail, v.compare, v.compare_mask, v.write_mask, v.reference);
}
void Encoder::put(const RD::PipelineColorBlendState &v) {
	structure(Structure::BLEND, v.enable_logic_op, v.logic_op, v.attachments, v.blend_constant);
}
void Encoder::put(const RD::PipelineColorBlendState::Attachment &v) {
	structure(Structure::BLEND_ATTACHMENT, v.enable_blend, v.src_color_blend_factor, v.dst_color_blend_factor, v.color_blend_op, v.src_alpha_blend_factor, v.dst_alpha_blend_factor, v.alpha_blend_op, v.write_r, v.write_g, v.write_b, v.write_a);
}
void Encoder::put(const RD::PipelineSpecializationConstant &v) {
	// Do not read an inactive union member or include padding bytes.
	switch (v.type) {
		case RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
			structure(Structure::SPECIALIZATION, v.type, v.constant_id, v.bool_value);
			break;
		case RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
			structure(Structure::SPECIALIZATION, v.type, v.constant_id, v.int_value);
			break;
		case RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
			structure(Structure::SPECIALIZATION, v.type, v.constant_id, v.float_value);
			break;
	}
}
}
#endif
