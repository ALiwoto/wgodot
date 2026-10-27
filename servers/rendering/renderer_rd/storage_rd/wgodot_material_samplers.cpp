// wgodot-changes::file
#include "material_storage.h"

using namespace RendererRD;

#ifdef TOOLS_ENABLED
void MaterialStorage::set_debug_authored_sampler_anisotropy_disabled(bool p_disabled) {
	if (debug_authored_sampler_anisotropy_disabled == p_disabled) {
		return;
	}
	debug_authored_sampler_anisotropy_disabled = p_disabled;
	// Render-thread boundary: replace existing material bindings once, never
	// scan materials during playback. Pending RID allocations have no data yet.
	for (const RID &rid : material_owner.get_owned_list(true)) {
		_material_queue_update(material_owner.get_or_null(rid), false, true);
	}
}
#endif

RID MaterialStorage::sampler_rd_get_authored(ShaderLanguage::TextureFilter p_filter, const WGodotSamplerState &p_state) {
	WGodotSamplerState state = p_state;
#ifdef TOOLS_ENABLED
	if (debug_authored_sampler_anisotropy_disabled) {
		state.anisotropy = 1;
	}
#endif
	uint32_t key = state.key(p_filter);
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
	sampler.use_anisotropy = state.anisotropy > 1;
	sampler.anisotropy_max = float(state.anisotropy);
	const RD::SamplerRepeatMode addresses[] = {
		RD::SAMPLER_REPEAT_MODE_REPEAT,
		RD::SAMPLER_REPEAT_MODE_MIRRORED_REPEAT,
		RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE,
	};
	sampler.repeat_u = addresses[state.address_u];
	sampler.repeat_v = addresses[state.address_v];
	sampler.repeat_w = addresses[state.address_w];
	RID result = RD::get_singleton()->sampler_create(sampler);
	authored_samplers.insert(key, result);
	return result;
}
