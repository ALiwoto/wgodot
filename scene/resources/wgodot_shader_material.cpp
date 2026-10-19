// wgodot-changes::file
#include "material.h"

#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/wgodot_shader_parameter_io.h"

void ShaderMaterial::_bind_typed_parameters() {
#define BIND_SHADER_PARAMETER(Name) \
	ClassDB::bind_method(D_METHOD("set_shader_parameter_" #Name, "name", "value"), &ShaderMaterial::set_shader_parameter_##Name); \
	ClassDB::bind_method(D_METHOD("get_shader_parameter_" #Name, "name"), &ShaderMaterial::get_shader_parameter_##Name);
	BIND_SHADER_PARAMETER(bool)
	BIND_SHADER_PARAMETER(int)
	BIND_SHADER_PARAMETER(float)
	BIND_SHADER_PARAMETER(vector2)
	BIND_SHADER_PARAMETER(vector3)
	BIND_SHADER_PARAMETER(vector4)
	BIND_SHADER_PARAMETER(vector2i)
	BIND_SHADER_PARAMETER(vector3i)
	BIND_SHADER_PARAMETER(vector4i)
	BIND_SHADER_PARAMETER(color)
	BIND_SHADER_PARAMETER(transform2d)
	BIND_SHADER_PARAMETER(basis)
	BIND_SHADER_PARAMETER(projection)
	BIND_SHADER_PARAMETER(transform3d)
	BIND_SHADER_PARAMETER(float32_array)
	BIND_SHADER_PARAMETER(float64_array)
	BIND_SHADER_PARAMETER(int32_array)
	BIND_SHADER_PARAMETER(vector2_array)
	BIND_SHADER_PARAMETER(vector3_array)
	BIND_SHADER_PARAMETER(vector4_array)
	BIND_SHADER_PARAMETER(color_array)
#undef BIND_SHADER_PARAMETER
	ClassDB::bind_method(D_METHOD("set_shader_parameter_texture", "name", "value"), &ShaderMaterial::set_shader_parameter_texture);
	ClassDB::bind_method(D_METHOD("get_shader_parameter_texture", "name"), &ShaderMaterial::get_shader_parameter_texture);
	ClassDB::bind_method(D_METHOD("get_shader_parameter_texture2d", "name"), &ShaderMaterial::get_shader_parameter_texture2d);
	ClassDB::bind_method(D_METHOD("get_shader_parameter_texture3d", "name"), &ShaderMaterial::get_shader_parameter_texture3d);
	ClassDB::bind_method(D_METHOD("get_shader_parameter_texture_layered", "name"), &ShaderMaterial::get_shader_parameter_texture_layered);
	ClassDB::bind_method(D_METHOD("get_shader_parameter_type", "name"), &ShaderMaterial::get_shader_parameter_type);
	ClassDB::bind_method(D_METHOD("clear_shader_parameter", "name"), &ShaderMaterial::clear_shader_parameter);
}

void ShaderMaterial::_ensure_parameters() const {
	const uint64_t revision = shader.is_valid() ? shader->get_parameter_revision() : 0;
	if (parameter_revision == revision) {
		return;
	}
	const Ref<WShaderParameterLayout> previous = parameters.layout;
	const Vector<Ref<Texture>> previous_textures = parameter_textures;
	const Ref<WShaderParameterLayout> layout = shader.is_valid() ? shader->get_parameter_layout() : Ref<WShaderParameterLayout>();
	if (layout.is_null() && shader.is_valid()) {
		// Preserve authored values while an edited shader has a syntax error.
		parameter_revision = shader->get_parameter_revision();
		return;
	}
	if (shader.is_null()) {
		parameters.reset(Ref<WShaderParameterLayout>());
	} else {
		parameters.remap(layout);
	}
	parameter_textures.clear();
	if (layout.is_valid()) {
		parameter_textures.resize(layout->defaults.textures.size());
		if (previous.is_valid()) {
			for (int slot = 0; slot < layout->entries.size(); slot++) {
				const auto &entry = layout->entries[slot];
				if (entry.texture < 0 || !parameters.buffer.overridden[slot]) {
					continue;
				}
				const int old_slot = previous->find(entry.name);
				if (old_slot >= 0 && layout->compatible(slot, *previous.ptr(), old_slot)) {
					for (int i = 0; i < entry.count; i++) {
						parameter_textures.write[entry.texture + i] = previous_textures[previous->entries[old_slot].texture + i];
					}
				}
			}
		}
	}
	parameter_revision = shader.is_valid() ? shader->get_parameter_revision() : 0;
}

void ShaderMaterial::_send_parameter(int p_slot) const {
	if (_get_material().is_valid()) {
		RS::get_singleton()->material_update_parameter(_get_material(), parameters.update(p_slot));
	}
}

int ShaderMaterial::get_shader_parameter_slot(const StringName &p_name) const {
	_ensure_parameters();
	ERR_FAIL_COND_V_MSG(parameters.layout.is_null(), -1, "ShaderMaterial has no valid shader parameter layout.");
	const int slot = parameters.layout->find(p_name);
	ERR_FAIL_COND_V_MSG(slot < 0, -1, "Shader parameter does not exist: " + p_name);
	return slot;
}

Variant::Type ShaderMaterial::get_shader_parameter_type(const StringName &p_name) const {
	_ensure_parameters();
	const int slot = parameters.layout.is_valid() ? parameters.layout->find(p_name) : -1;
	return slot >= 0 ? parameters.layout->entries[slot].value_type : Variant::NIL;
}

const WShaderParameters &ShaderMaterial::get_shader_parameters() const {
	_ensure_parameters();
	return parameters;
}

void ShaderMaterial::clear_shader_parameter(const StringName &p_name) {
	const int slot = get_shader_parameter_slot(p_name);
	ERR_FAIL_COND(slot < 0);
	const auto &entry = parameters.layout->entries[slot];
	if (entry.texture >= 0) {
		for (int i = 0; i < entry.count; i++) {
			parameter_textures.write[entry.texture + i].unref();
		}
	}
	parameters.clear(slot);
	_send_parameter(slot);
}

void ShaderMaterial::mark_shader_parameter_overrides(std::initializer_list<int> p_slots) {
	_ensure_parameters();
	ERR_FAIL_COND(parameters.layout.is_null());
	for (int slot : p_slots) {
		ERR_FAIL_INDEX(slot, parameters.layout->entries.size());
		ERR_FAIL_COND(!parameters.layout->entries[slot].valid() || parameters.layout->entries[slot].texture >= 0);
		parameters.buffer.overridden.write[slot] = 1;
		_send_parameter(slot);
	}
}

namespace {
bool texture_matches(ShaderLanguage::DataType p_type, const Ref<Texture> &p_texture) {
	if (p_texture.is_null()) {
		return true;
	}
	using SL = ShaderLanguage;
	switch (p_type) {
		case SL::TYPE_SAMPLEREXT:
		case SL::TYPE_SAMPLER2D:
		case SL::TYPE_ISAMPLER2D:
		case SL::TYPE_USAMPLER2D:
			return Object::cast_to<Texture2D>(p_texture.ptr()) != nullptr;
		case SL::TYPE_SAMPLER3D:
		case SL::TYPE_ISAMPLER3D:
		case SL::TYPE_USAMPLER3D:
			return Object::cast_to<Texture3D>(p_texture.ptr()) != nullptr;
		default: {
			const TextureLayered *layered = Object::cast_to<TextureLayered>(p_texture.ptr());
			if (!layered) {
				return false;
			}
			if (p_type == SL::TYPE_SAMPLERCUBE) {
				return layered->get_layered_type() == TextureLayered::LAYERED_TYPE_CUBEMAP;
			}
			if (p_type == SL::TYPE_SAMPLERCUBEARRAY) {
				return layered->get_layered_type() == TextureLayered::LAYERED_TYPE_CUBEMAP_ARRAY;
			}
			return layered->get_layered_type() == TextureLayered::LAYERED_TYPE_2D_ARRAY;
		}
	}
}
} // namespace

void ShaderMaterial::set_shader_parameter_at(int p_slot, const Ref<Texture> &p_texture) {
	_ensure_parameters();
	ERR_FAIL_COND(parameters.layout.is_null());
	ERR_FAIL_INDEX(p_slot, parameters.layout->entries.size());
	const auto &entry = parameters.layout->entries[p_slot];
	ERR_FAIL_COND_MSG(entry.texture < 0 || entry.array || !texture_matches(entry.type, p_texture), "Shader texture type mismatch: " + entry.name);
	// Null means revert to the shader's default, matching the Resource API.
	parameter_textures.write[entry.texture] = p_texture;
	parameters.buffer.textures.write[entry.texture] = p_texture.is_valid() ? p_texture->get_rid() : RID();
	parameters.buffer.overridden.write[p_slot] = p_texture.is_valid();
	parameters.buffer.texture_counts.write[p_slot] = p_texture.is_valid() ? 1 : 0;
	_send_parameter(p_slot);
}

void ShaderMaterial::set_shader_parameter_at(int p_slot, const Vector<Ref<Texture>> &p_textures) {
	_ensure_parameters();
	ERR_FAIL_COND(parameters.layout.is_null());
	ERR_FAIL_INDEX(p_slot, parameters.layout->entries.size());
	const auto &entry = parameters.layout->entries[p_slot];
	ERR_FAIL_COND_MSG(entry.texture < 0 || !entry.array, "Shader parameter is not a texture array: " + entry.name);
	for (const Ref<Texture> &texture : p_textures) {
		ERR_FAIL_COND_MSG(!texture_matches(entry.type, texture), "Shader texture array element type mismatch: " + entry.name);
	}
	for (int i = 0; i < entry.count; i++) {
		const Ref<Texture> texture = i < p_textures.size() ? p_textures[i] : Ref<Texture>();
		parameter_textures.write[entry.texture + i] = texture;
		parameters.buffer.textures.write[entry.texture + i] = texture.is_valid() ? texture->get_rid() : RID();
	}
	parameters.buffer.overridden.write[p_slot] = 1;
	parameters.buffer.texture_counts.write[p_slot] = MIN(entry.count, p_textures.size());
	_send_parameter(p_slot);
}

Ref<Texture> ShaderMaterial::get_shader_texture_at(int p_slot, int p_index) const {
	_ensure_parameters();
	ERR_FAIL_COND_V(parameters.layout.is_null(), Ref<Texture>());
	ERR_FAIL_INDEX_V(p_slot, parameters.layout->entries.size(), Ref<Texture>());
	const auto &entry = parameters.layout->entries[p_slot];
	ERR_FAIL_COND_V_MSG(entry.texture < 0, Ref<Texture>(), "Shader parameter is not a texture: " + entry.name);
	ERR_FAIL_INDEX_V(p_index, entry.count, Ref<Texture>());
	if (!parameters.buffer.overridden[p_slot] || p_index >= parameters.buffer.texture_counts[p_slot]) {
		return shader->get_default_texture_parameter(entry.name, p_index);
	}
	return parameter_textures[entry.texture + p_index];
}

void ShaderMaterial::set_shader_parameter_texture(const StringName &p_name, const Ref<Texture> &p_value) {
	set_shader_parameter_at(get_shader_parameter_slot(p_name), p_value);
}

Ref<Texture> ShaderMaterial::get_shader_parameter_texture(const StringName &p_name) const {
	const int slot = get_shader_parameter_slot(p_name);
	ERR_FAIL_COND_V(slot < 0, Ref<Texture>());
	ERR_FAIL_COND_V_MSG(parameters.layout->entries[slot].array, Ref<Texture>(), "A texture-array parameter requires indexed access: " + p_name);
	return get_shader_texture_at(slot);
}

Ref<Texture2D> ShaderMaterial::get_shader_parameter_texture2d(const StringName &p_name) const {
	const Ref<Texture> texture = get_shader_parameter_texture(p_name);
	const Ref<Texture2D> result = texture;
	ERR_FAIL_COND_V_MSG(texture.is_valid() && result.is_null(), Ref<Texture2D>(), "Shader parameter is not a Texture2D: " + p_name);
	return result;
}

Ref<Texture3D> ShaderMaterial::get_shader_parameter_texture3d(const StringName &p_name) const {
	const Ref<Texture> texture = get_shader_parameter_texture(p_name);
	const Ref<Texture3D> result = texture;
	ERR_FAIL_COND_V_MSG(texture.is_valid() && result.is_null(), Ref<Texture3D>(), "Shader parameter is not a Texture3D: " + p_name);
	return result;
}

Ref<TextureLayered> ShaderMaterial::get_shader_parameter_texture_layered(const StringName &p_name) const {
	const Ref<Texture> texture = get_shader_parameter_texture(p_name);
	const Ref<TextureLayered> result = texture;
	ERR_FAIL_COND_V_MSG(texture.is_valid() && result.is_null(), Ref<TextureLayered>(), "Shader parameter is not a TextureLayered: " + p_name);
	return result;
}

void ShaderMaterial::set_shader_parameter(const StringName &p_param, const Variant &p_value) {
	const int slot = get_shader_parameter_slot(p_param);
	ERR_FAIL_COND(slot < 0);
	if (p_value.get_type() == Variant::NIL) {
		clear_shader_parameter(p_param);
		return;
	}
	const auto &entry = parameters.layout->entries[slot];
	if (entry.texture >= 0) {
		if (entry.array) {
			ERR_FAIL_COND_MSG(p_value.get_type() != Variant::ARRAY, "A shader texture array requires an Array of textures.");
			const Array values = p_value;
			Vector<Ref<Texture>> textures;
			textures.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				textures.write[i] = values[i];
				ERR_FAIL_COND_MSG(!values[i].is_zero() && textures[i].is_null(), "A shader texture array contains a non-texture value.");
			}
			set_shader_parameter_at(slot, textures);
		} else {
			const Ref<Texture> texture = p_value;
			ERR_FAIL_COND_MSG(!p_value.is_zero() && texture.is_null(), "A shader sampler requires a Texture resource.");
			set_shader_parameter_at(slot, texture);
		}
		return;
	}
	if (WShaderParameterIO::write(parameters, slot, p_value)) {
		_send_parameter(slot);
	}
}

Variant ShaderMaterial::get_shader_parameter(const StringName &p_param) const {
	const int slot = get_shader_parameter_slot(p_param);
	ERR_FAIL_COND_V(slot < 0, Variant());
	const auto &entry = parameters.layout->entries[slot];
	if (entry.texture >= 0) {
		if (!entry.array) {
			return get_shader_texture_at(slot);
		}
		TypedArray<Texture> textures;
		const int count = parameters.buffer.overridden[slot] ? parameters.buffer.texture_counts[slot] : entry.count;
		textures.resize(count);
		for (int i = 0; i < count; i++) {
			textures[i] = get_shader_texture_at(slot, i);
		}
		return textures;
	}
	return WShaderParameterIO::read(parameters, slot);
}
