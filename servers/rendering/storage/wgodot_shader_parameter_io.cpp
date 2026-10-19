// wgodot-changes::file
#include "wgodot_shader_parameter_io.h"

using SL = ShaderLanguage;

bool WShaderParameterIO::write(WShaderParameters &r_parameters, int p_slot, const Variant &p_value) {
	ERR_FAIL_COND_V(r_parameters.layout.is_null(), false);
	ERR_FAIL_INDEX_V(p_slot, r_parameters.layout->entries.size(), false);
	const auto &entry = r_parameters.layout->entries[p_slot];
	if (p_value.get_type() == Variant::NIL) {
		r_parameters.clear(p_slot);
		return true;
	}
	if (entry.texture >= 0) {
		Vector<RID> textures;
		if (p_value.get_type() == Variant::RID) {
			textures.push_back(p_value);
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array source = p_value;
			for (const Variant &texture : source) {
				ERR_FAIL_COND_V(texture.get_type() != Variant::RID && texture.get_type() != Variant::NIL, false);
				textures.push_back(texture.get_type() == Variant::NIL ? RID() : RID(texture));
			}
		} else {
			ERR_FAIL_V_MSG(false, "A rendering-server texture parameter requires a texture RID.");
		}
		for (int i = 0; i < entry.count; i++) {
			r_parameters.buffer.textures.write[entry.texture + i] = i < textures.size() ? textures[i] : RID();
		}
		r_parameters.buffer.overridden.write[p_slot] = 1;
		r_parameters.buffer.texture_counts.write[p_slot] = MIN(entry.count, textures.size());
		return true;
	}
	if (entry.array) {
		if (p_value.get_type() == Variant::PACKED_COLOR_ARRAY) {
			return r_parameters.set(p_slot, PackedColorArray(p_value));
		}
		if (p_value.get_type() == Variant::ARRAY) {
			const Array values = p_value;
			bool has_colors = false;
			bool has_other_values = false;
			for (const Variant &value : values) {
				has_colors |= value.get_type() == Variant::COLOR;
				has_other_values |= value.get_type() != Variant::COLOR;
			}
			if (has_colors) {
				ERR_FAIL_COND_V_MSG(has_other_values, false, "A shader array cannot mix Color and raw numeric values. Choose PackedColorArray or a packed vector array.");
				return r_parameters.set(p_slot, PackedColorArray(p_value));
			}
		}
		if (entry.type == SL::TYPE_FLOAT) {
			return r_parameters.set(p_slot, PackedFloat32Array(p_value));
		}
		if (WShaderPacking::integer(entry.type) || WShaderPacking::boolean(entry.type)) {
			PackedInt32Array values;
			switch (WShaderPacking::components(entry.type)) {
				case 2:
					values = convert_array_std140<Vector2i, int32_t>(p_value);
					break;
				case 3:
					values = convert_array_std140<Vector3i, int32_t>(p_value);
					break;
				case 4:
					values = convert_array_std140<Vector4i, int32_t>(p_value);
					break;
				default:
					values = p_value;
					break;
			}
			return r_parameters.set(p_slot, values);
		}
		PackedFloat32Array values;
		switch (entry.type) {
			case SL::TYPE_VEC2:
				values = convert_array_std140<Vector2, float>(p_value);
				break;
			case SL::TYPE_VEC3:
				values = convert_array_std140<Vector3, float>(p_value);
				break;
			case SL::TYPE_VEC4:
				values = convert_array_std140<Vector4, float>(p_value);
				break;
			case SL::TYPE_MAT3:
				values = convert_array_std140<Basis, float>(p_value);
				break;
			case SL::TYPE_MAT4:
				values = convert_array_std140<Projection, float>(p_value);
				break;
			default:
				values = p_value;
				break;
		}
		return r_parameters.set(p_slot, values);
	}
	if (p_value.get_type() == Variant::COLOR && (entry.type == SL::TYPE_VEC3 || entry.type == SL::TYPE_VEC4)) {
		return r_parameters.set(p_slot, Color(p_value));
	}
	switch (entry.type) {
		case SL::TYPE_BOOL:
			return r_parameters.set(p_slot, bool(p_value));
		case SL::TYPE_BVEC2:
		case SL::TYPE_BVEC3:
		case SL::TYPE_BVEC4:
		case SL::TYPE_INT:
		case SL::TYPE_UINT:
			return r_parameters.set(p_slot, int64_t(p_value));
		case SL::TYPE_FLOAT:
			return r_parameters.set(p_slot, double(p_value));
		case SL::TYPE_IVEC2:
		case SL::TYPE_UVEC2:
			return r_parameters.set(p_slot, convert_to_vector<Vector2i>(p_value));
		case SL::TYPE_IVEC3:
		case SL::TYPE_UVEC3:
			return r_parameters.set(p_slot, convert_to_vector<Vector3i>(p_value));
		case SL::TYPE_IVEC4:
		case SL::TYPE_UVEC4:
			return r_parameters.set(p_slot, convert_to_vector<Vector4i>(p_value));
		case SL::TYPE_VEC2:
			return r_parameters.set(p_slot, convert_to_vector<Vector2>(p_value));
		case SL::TYPE_VEC3:
			return r_parameters.set(p_slot, convert_to_vector<Vector3>(p_value));
		case SL::TYPE_VEC4:
			return r_parameters.set(p_slot, convert_to_vector<Vector4>(p_value));
		case SL::TYPE_MAT2:
			return r_parameters.set(p_slot, Transform2D(p_value));
		case SL::TYPE_MAT3:
			return r_parameters.set(p_slot, Basis(p_value));
		case SL::TYPE_MAT4:
			return r_parameters.set(p_slot, Projection(p_value));
		default:
			ERR_FAIL_V_MSG(false, "Unsupported shader declaration: " + entry.name);
	}
}

Variant WShaderParameterIO::read(const WShaderParameters &p_parameters, int p_slot) {
	ERR_FAIL_COND_V(p_parameters.layout.is_null(), Variant());
	ERR_FAIL_INDEX_V(p_slot, p_parameters.layout->entries.size(), Variant());
	const auto &entry = p_parameters.layout->entries[p_slot];
	if (entry.texture >= 0) {
		if (!entry.array) {
			return p_parameters.buffer.textures[entry.texture];
		}
		Array textures;
		const int count = p_parameters.buffer.overridden[p_slot] ? p_parameters.buffer.texture_counts[p_slot] : entry.count;
		textures.resize(count);
		for (int i = 0; i < count; i++) {
			textures[i] = p_parameters.buffer.textures[entry.texture + i];
		}
		return textures;
	}
	if (entry.array) {
		if (p_parameters.buffer.colors[p_slot]) {
			return p_parameters.get<PackedColorArray>(p_slot);
		}
		if (WShaderPacking::integer(entry.type) || WShaderPacking::boolean(entry.type)) {
			return p_parameters.get<PackedInt32Array>(p_slot);
		}
		switch (entry.type) {
			case SL::TYPE_VEC2:
				return p_parameters.get<PackedVector2Array>(p_slot);
			case SL::TYPE_VEC3:
				return p_parameters.get<PackedVector3Array>(p_slot);
			case SL::TYPE_VEC4:
				return p_parameters.get<PackedVector4Array>(p_slot);
			default:
				return p_parameters.get<PackedFloat32Array>(p_slot);
		}
	}
	if (p_parameters.buffer.colors[p_slot]) {
		return p_parameters.get<Color>(p_slot);
	}
	switch (entry.type) {
		case SL::TYPE_BOOL:
			return p_parameters.get<bool>(p_slot);
		case SL::TYPE_BVEC2:
		case SL::TYPE_BVEC3:
		case SL::TYPE_BVEC4:
		case SL::TYPE_INT:
		case SL::TYPE_UINT:
			return p_parameters.get<int64_t>(p_slot);
		case SL::TYPE_FLOAT:
			return p_parameters.get<double>(p_slot);
		case SL::TYPE_IVEC2:
		case SL::TYPE_UVEC2:
			return p_parameters.get<Vector2i>(p_slot);
		case SL::TYPE_IVEC3:
		case SL::TYPE_UVEC3:
			return p_parameters.get<Vector3i>(p_slot);
		case SL::TYPE_IVEC4:
		case SL::TYPE_UVEC4:
			return p_parameters.get<Vector4i>(p_slot);
		case SL::TYPE_VEC2:
			return p_parameters.get<Vector2>(p_slot);
		case SL::TYPE_VEC3:
			return p_parameters.get<Vector3>(p_slot);
		case SL::TYPE_VEC4:
			return p_parameters.get<Vector4>(p_slot);
		case SL::TYPE_MAT2:
			return p_parameters.get<Transform2D>(p_slot);
		case SL::TYPE_MAT3:
			return p_parameters.get<Basis>(p_slot);
		case SL::TYPE_MAT4:
			return p_parameters.get<Projection>(p_slot);
		default:
			ERR_FAIL_V_MSG(Variant(), "Unsupported shader declaration: " + entry.name);
	}
}
