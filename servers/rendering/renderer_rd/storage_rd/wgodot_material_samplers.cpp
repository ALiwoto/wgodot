// wgodot-changes::file
#include "material_storage.h"

using namespace RendererRD;

RID MaterialStorage::sampler_rd_get_authored(ShaderLanguage::TextureFilter p_filter, const WGodotSamplerState &p_state) {
	uint32_t key = p_state.key(p_filter);
	if (const RID *cached = authored_samplers.getptr(key)) {
		return *cached;
	}
	RD::SamplerState sampler;
	bool linear = p_filter == ShaderLanguage::FILTER_LINEAR ||
			p_filter == ShaderLanguage::FILTER_LINEAR_MIPMAP ||
			p_filter == ShaderLanguage::FILTER_LINEAR_MIPMAP_ANISOTROPIC;
	sampler.min_filter = linear ? RD::SAMPLER_FILTER_LINEAR : RD::SAMPLER_FILTER_NEAREST;
	sampler.mag_filter = sampler.min_filter;
	sampler.mip_filter = RD::SAMPLER_FILTER_LINEAR;
	if (p_filter == ShaderLanguage::FILTER_LINEAR || p_filter == ShaderLanguage::FILTER_NEAREST) {
		sampler.max_lod = 0;
	}
	sampler.use_anisotropy = p_state.anisotropy > 1;
	sampler.anisotropy_max = float(p_state.anisotropy);
	const RD::SamplerRepeatMode addresses[] = {
		RD::SAMPLER_REPEAT_MODE_REPEAT,
		RD::SAMPLER_REPEAT_MODE_MIRRORED_REPEAT,
		RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE,
	};
	sampler.repeat_u = addresses[p_state.address_u];
	sampler.repeat_v = addresses[p_state.address_v];
	sampler.repeat_w = addresses[p_state.address_w];
	RID result = RD::get_singleton()->sampler_create(sampler);
	authored_samplers.insert(key, result);
	return result;
}
