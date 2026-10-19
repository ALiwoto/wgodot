// wgodot-changes::file
#include "material_storage.h"

Ref<WShaderParameterLayout> RendererRD::MaterialStorage::shader_get_parameter_layout(RID p_shader) const {
	const Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, Ref<WShaderParameterLayout>());
	return shader->data ? shader->data->parameter_layout : Ref<WShaderParameterLayout>();
}

void RendererRD::MaterialStorage::material_set_parameters(RID p_material, const WShaderParameters &p_parameters) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);
	material->params.named.clear();
	material->params.typed = p_parameters;
	if (material->shader && material->shader->data) {
		material->params.typed.remap(material->shader->data->parameter_layout);
	}
	_material_queue_update(material, true, true);
}

void RendererRD::MaterialStorage::material_update_parameter(RID p_material, const WShaderParameterUpdate &p_update) {
	Material *material = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(material);
	ERR_FAIL_COND(p_update.layout.is_null());
	ERR_FAIL_INDEX(p_update.slot, p_update.layout->entries.size());
	material->params.typed.apply(p_update);
	if (material->shader && material->shader->data) {
		material->params.typed.remap(material->shader->data->parameter_layout);
	}
	const bool texture = p_update.layout->entries[p_update.slot].texture >= 0;
	_material_queue_update(material, !texture, texture);
}
