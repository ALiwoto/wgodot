// wgodot-changes::file
#include "wgodot_instance_shader_parameters.h"

WInstanceShaderValue WInstanceShaderValue::from_uniform(const ShaderLanguage::ShaderNode::Uniform &p_uniform) {
	WInstanceShaderValue result;
	const int width = WShaderPacking::components(p_uniform.type);
	ERR_FAIL_COND_V(width < 1 || width > 4 || WShaderPacking::matrix_columns(p_uniform.type), result);
	result.type = p_uniform.type;
	result.source_type = ShaderLanguage::uniform_to_property_info(p_uniform).type;
	for (int i = 0; i < width; i++) {
		const auto *value = i < p_uniform.default_value.size() ? &p_uniform.default_value[i] : nullptr;
		if (WShaderPacking::boolean(result.type)) {
			result.words[i] = value && value->boolean ? 1 : 0;
		} else if (WShaderPacking::integer(result.type)) {
			result.words[i] = value ? (result.type >= ShaderLanguage::TYPE_UINT ? value->uint : uint32_t(value->sint)) : 0;
		} else {
			const float fallback = result.source_type == Variant::COLOR && i == 3 ? 1.0f : 0.0f;
			WShaderPacking::write_float(result.words[i], value ? value->real : fallback);
		}
	}
	memcpy(result.linear_words, result.words, sizeof(result.words));
	if (result.source_type == Variant::COLOR && p_uniform.hint != ShaderLanguage::ShaderNode::Uniform::HINT_COLOR_CONVERSION_DISABLED) {
		WShaderParameterLayout::Entry entry;
		entry.type = result.type;
		const Color color = WShaderPacking::Access<Color>::read(entry, result.words);
		WShaderPacking::Access<Color>::write(entry, result.linear_words, color, true);
	}
	return result;
}

WInstanceShaderValue WInstanceShaderValue::from_variant(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL: return {};
		case Variant::BOOL: return pack(bool(p_value));
		case Variant::INT: return pack(int64_t(p_value));
		case Variant::FLOAT: return pack(double(p_value));
		case Variant::VECTOR2: return pack(Vector2(p_value));
		case Variant::VECTOR3: return pack(Vector3(p_value));
		case Variant::VECTOR4: return pack(Vector4(p_value));
		case Variant::VECTOR2I: return pack(Vector2i(p_value));
		case Variant::VECTOR3I: return pack(Vector3i(p_value));
		case Variant::VECTOR4I: return pack(Vector4i(p_value));
		case Variant::COLOR: return pack(Color(p_value));
		case Variant::RECT2: {
			const Rect2 value = p_value;
			auto result = pack(Vector4(value.position.x, value.position.y, value.size.x, value.size.y));
			result.source_type = Variant::RECT2;
			return result;
		}
		case Variant::RECT2I: {
			const Rect2i value = p_value;
			auto result = pack(Vector4i(value.position.x, value.position.y, value.size.x, value.size.y));
			result.source_type = Variant::RECT2I;
			return result;
		}
		case Variant::PLANE: {
			const Plane value = p_value;
			auto result = pack(Vector4(value.normal.x, value.normal.y, value.normal.z, value.d));
			result.source_type = Variant::PLANE;
			return result;
		}
		case Variant::QUATERNION: {
			const Quaternion value = p_value;
			auto result = pack(Vector4(value.x, value.y, value.z, value.w));
			result.source_type = Variant::QUATERNION;
			return result;
		}
		default: ERR_FAIL_V_MSG(WInstanceShaderValue(), "Unsupported instance-uniform value.");
	}
}

Variant WInstanceShaderValue::to_variant() const {
	if (!valid()) {
		return Variant();
	}
	switch (source_type) {
		case Variant::BOOL: return unpack<bool>();
		case Variant::INT: return unpack<int64_t>();
		case Variant::FLOAT: return unpack<double>();
		case Variant::VECTOR2: return unpack<Vector2>();
		case Variant::VECTOR3: return unpack<Vector3>();
		case Variant::VECTOR4: return unpack<Vector4>();
		case Variant::VECTOR2I: return unpack<Vector2i>();
		case Variant::VECTOR3I: return unpack<Vector3i>();
		case Variant::VECTOR4I: return unpack<Vector4i>();
		case Variant::COLOR: return unpack<Color>();
		case Variant::RECT2: {
			const Vector4 value = unpack<Vector4>();
			return Rect2(value.x, value.y, value.z, value.w);
		}
		case Variant::RECT2I: {
			const Vector4i value = unpack<Vector4i>();
			return Rect2i(value.x, value.y, value.z, value.w);
		}
		case Variant::PLANE: {
			const Vector4 value = unpack<Vector4>();
			return Plane(Vector3(value.x, value.y, value.z), value.w);
		}
		case Variant::QUATERNION: {
			const Vector4 value = unpack<Vector4>();
			return Quaternion(value.x, value.y, value.z, value.w);
		}
		default: ERR_FAIL_V(Variant());
	}
}

bool WInstanceShaderValue::convert_to(ShaderLanguage::DataType p_type) {
	if (type == p_type) {
		return true;
	}
	WShaderParameterLayout::Entry entry;
	entry.type = p_type;
	if (WShaderPacking::boolean(p_type) && type == ShaderLanguage::TYPE_INT) {
		const int64_t value = unpack<int64_t>();
		WShaderPacking::Access<int64_t>::write(entry, words, value, false);
	} else if (WShaderPacking::integer(type) && WShaderPacking::integer(p_type) && WShaderPacking::components(type) == WShaderPacking::components(p_type)) {
		// Signed/unsigned shader integers share their 32-bit representation.
	} else if (WShaderPacking::components(type) == 1 && WShaderPacking::components(p_type) == 1) {
		const double value = unpack<double>();
		if (p_type == ShaderLanguage::TYPE_FLOAT) {
			WShaderPacking::write_float(words[0], float(value));
		} else if (p_type == ShaderLanguage::TYPE_BOOL) {
			words[0] = value != 0.0;
		} else {
			words[0] = uint32_t(int64_t(value));
		}
	} else if (source_type == Variant::COLOR && p_type == ShaderLanguage::TYPE_VEC3) {
		// A color may target a vec3; the shader consumes RGB only.
		type = p_type;
		return true;
	} else {
		return false;
	}
	type = p_type;
	memcpy(linear_words, words, sizeof(words));
	return true;
}

int WInstanceShaderParameters::find_or_add(const StringName &p_name) {
	MutexLock lock(mutex);
	if (const int *slot = names.getptr(p_name)) {
		return *slot;
	}
	const int slot = items.size();
	Item item;
	item.info.name = p_name;
	items.push_back(item);
	names.insert(p_name, slot);
	return slot;
}

int WInstanceShaderParameters::find(const StringName &p_name) const {
	MutexLock lock(mutex);
	const int *slot = names.getptr(p_name);
	return slot ? *slot : -1;
}

void WInstanceShaderParameters::invalidate() {
	MutexLock lock(mutex);
	for (Item &item : items) {
		item.gpu_index = -1;
	}
}

void WInstanceShaderParameters::define(const PropertyInfo &p_info, int p_gpu_index, const WInstanceShaderValue &p_default) {
	const int slot = find_or_add(p_info.name);
	MutexLock lock(mutex);
	Item &item = items.write[slot];
	if (item.gpu_index >= 0) {
		if (item.gpu_index != p_gpu_index || item.default_value.type != p_default.type) {
			WARN_PRINT("Materials on one instance disagree about instance uniform '" + p_info.name + "'. The first declaration is used.");
		}
		return;
	}
	item.info = p_info;
	item.gpu_index = p_gpu_index;
	item.default_value = p_default;
	if (!item.overridden || !item.value.convert_to(p_default.type)) {
		item.value = p_default;
		item.overridden = false;
	}
}

Vector<WInstanceShaderParameters::Item> WInstanceShaderParameters::snapshot() const {
	MutexLock lock(mutex);
	return items;
}

WInstanceShaderParameters::Item WInstanceShaderParameters::get(int p_slot) const {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_slot, items.size(), Item());
	return items[p_slot];
}

WInstanceShaderParameters::SetResult WInstanceShaderParameters::set(int p_slot, WInstanceShaderValue &r_value) {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_slot, items.size(), SetResult::INVALID);
	ERR_FAIL_COND_V(!r_value.valid(), SetResult::INVALID);
	const Item &previous = items[p_slot];
	ERR_FAIL_COND_V_MSG(previous.gpu_index >= 0 && !r_value.convert_to(previous.default_value.type), SetResult::INVALID, "Instance-uniform value does not match its shader declaration: " + previous.info.name);
	// Compare the exact typed GPU payload before requesting writable storage.
	// Snapshots share this vector: even an unchanged write would otherwise
	// clone it and enqueue another renderer upload. Preserve override identity
	// and both color spaces; approximate equality could erase visible changes.
	if (previous.overridden && previous.value.type == r_value.type && previous.value.source_type == r_value.source_type &&
			memcmp(previous.value.words, r_value.words, sizeof(r_value.words)) == 0 &&
			memcmp(previous.value.linear_words, r_value.linear_words, sizeof(r_value.linear_words)) == 0) {
		return SetResult::UNCHANGED;
	}
	Item &item = items.write[p_slot];
	item.value = r_value;
	item.overridden = true;
	return SetResult::CHANGED;
}

WInstanceShaderValue WInstanceShaderParameters::value(int p_slot) const {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_slot, items.size(), WInstanceShaderValue());
	return items[p_slot].value;
}

WInstanceShaderValue WInstanceShaderParameters::clear(int p_slot) {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_slot, items.size(), WInstanceShaderValue());
	Item &item = items.write[p_slot];
	item.value = item.default_value;
	item.overridden = false;
	return item.value;
}

int WInstanceShaderParameters::gpu_index(int p_slot, WInstanceShaderValue &r_value) const {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_slot, items.size(), -1);
	const Item &item = items[p_slot];
	if (item.gpu_index < 0 || !r_value.convert_to(item.default_value.type)) {
		return -1;
	}
	return item.gpu_index;
}
