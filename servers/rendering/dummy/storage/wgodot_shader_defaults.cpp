// wgodot-changes::file
#include "material_storage.h"

Variant RendererDummy::MaterialStorage::shader_get_parameter_default(RID p_shader, const StringName &p_param) const {
	const DummyShader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, Variant());
	const ShaderLanguage::ShaderNode::Uniform *uniform = shader->uniforms.getptr(p_param);
	if (!uniform) {
		return Variant();
	}
	if (uniform->default_value.is_empty()) {
		return ShaderLanguage::get_default_datatype_value(uniform->type, uniform->array_size, uniform->hint);
	}
	return ShaderLanguage::constant_value_to_variant(uniform->default_value, uniform->type, uniform->array_size, uniform->hint);
}

Ref<WShaderParameterLayout> RendererDummy::MaterialStorage::shader_get_parameter_layout(RID p_shader) const {
	const DummyShader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, Ref<WShaderParameterLayout>());
	return shader->parameter_layout;
}

void RendererDummy::MaterialStorage::shader_set_default_texture_parameter(RID p_shader, const StringName &p_name, RID p_texture, int p_index) {
	DummyShader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);
	if (p_texture.is_valid()) {
		shader->default_textures[p_name][p_index] = p_texture;
	} else if (auto *textures = shader->default_textures.getptr(p_name)) {
		textures->erase(p_index);
		if (textures->is_empty()) {
			shader->default_textures.erase(p_name);
		}
	}
	if (shader->parameter_layout.is_valid()) {
		shader->parameter_layout = shader->parameter_layout->with_texture_default(p_name, p_index, p_texture);
	}
}

RID RendererDummy::MaterialStorage::shader_get_default_texture_parameter(RID p_shader, const StringName &p_name, int p_index) const {
	const DummyShader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, RID());
	const auto *textures = shader->default_textures.getptr(p_name);
	const RID *texture = textures ? textures->getptr(p_index) : nullptr;
	return texture ? *texture : RID();
}
