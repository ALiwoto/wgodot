// wgodot-changes::file
#include "wgodot_shader_parameters.h"

Ref<WShaderParameterLayout> WShaderParameterLayout::create(const HashMap<StringName, Uniform> &p_uniforms, const Vector<uint32_t> &p_offsets, uint32_t p_size) {
	using SL = ShaderLanguage;
	Ref<WShaderParameterLayout> result = memnew(WShaderParameterLayout);
	int slots = 0;
	int texture_orders = 0;
	for (const auto &item : p_uniforms) {
		const Uniform &uniform = item.value;
		if (uniform.scope == Uniform::SCOPE_LOCAL) {
			slots = MAX(slots, uniform.prop_order + 1);
		}
		if (uniform.is_texture()) {
			texture_orders = MAX(texture_orders, uniform.texture_order + 1);
		}
	}
	result->entries.resize(slots);
	result->texture_entries.resize(texture_orders);
	result->texture_entries.fill(-1);
	result->defaults.words.resize(p_size / sizeof(uint32_t));
	result->defaults.words.fill(0);
	result->defaults.overridden.resize(slots);
	result->defaults.overridden.fill(0);
	result->defaults.texture_counts.resize(slots);
	result->defaults.texture_counts.fill(0);
	result->defaults.colors.resize(slots);
	result->defaults.colors.fill(0);
	int textures = 0;
	for (const auto &item : p_uniforms) {
		const Uniform &uniform = item.value;
		if (uniform.scope != Uniform::SCOPE_LOCAL) {
			continue;
		}
		Entry &entry = result->entries.write[uniform.prop_order];
		entry.name = item.key;
		entry.type = uniform.type;
		entry.hint = uniform.hint;
		entry.value_type = SL::uniform_to_property_info(uniform).type;
		if (entry.value_type == Variant::RID) {
			entry.value_type = Variant::OBJECT;
		}
		entry.array = uniform.array_size > 0;
		entry.count = MAX(1, uniform.array_size);
		result->names.insert(item.key, uniform.prop_order);
		if (uniform.is_texture()) {
			entry.texture = textures;
			textures += entry.count;
			result->texture_entries.write[uniform.texture_order] = uniform.prop_order;
			continue;
		}
		ERR_FAIL_INDEX_V(uniform.order, p_offsets.size(), Ref<WShaderParameterLayout>());
		entry.offset = p_offsets[uniform.order] / sizeof(uint32_t);
		entry.stride = SL::get_datatype_size(uniform.type) / sizeof(uint32_t);
		if (entry.array) {
			entry.stride = (entry.stride + 3) & ~3;
		}
		ERR_FAIL_COND_V(entry.offset + entry.stride * entry.count > result->defaults.words.size(), Ref<WShaderParameterLayout>());
		uint32_t *words = result->defaults.words.ptrw() + entry.offset;
		const int width = WShaderPacking::components(entry.type);
		const int columns = WShaderPacking::matrix_columns(entry.type);
		const bool color = SL::is_hint_color(entry.hint);
		result->defaults.colors.write[uniform.prop_order] = color;
		for (int element = 0; element < entry.count; element++) {
			for (int component = 0; component < width; component++) {
				const int source = element * width + component;
				const SL::Scalar *value = source < uniform.default_value.size() ? &uniform.default_value[source] : nullptr;
				uint32_t &word = words[element * entry.stride + WShaderPacking::word_index(entry.type, component)];
				if (WShaderPacking::boolean(entry.type)) {
					word = value && value->boolean ? 1 : 0;
				} else if (WShaderPacking::integer(entry.type)) {
					word = value ? (entry.type >= SL::TYPE_UINT ? value->uint : uint32_t(value->sint)) : 0;
				} else {
					const float fallback = (columns && component / columns == component % columns) || (color && !entry.array && component == 3) ? 1.0f : 0.0f;
					WShaderPacking::write_float(word, value ? value->real : fallback);
				}
			}
		}
	}
	result->defaults.textures.resize(textures);
	result->defaults.linear_words = result->defaults.words;
	for (int slot = 0; slot < result->entries.size(); slot++) {
		const Entry &entry = result->entries[slot];
		if (entry.hint != Uniform::HINT_SOURCE_COLOR || entry.texture >= 0) {
			continue;
		}
		for (int element = 0; element < entry.count; element++) {
			const int offset = entry.offset + element * entry.stride;
			const Color color = WShaderPacking::Access<Color>::read(entry, result->defaults.words.ptr() + offset);
			WShaderPacking::Access<Color>::write(entry, result->defaults.linear_words.ptrw() + offset, color, true);
		}
	}
	return result;
}

int WShaderParameterLayout::find(const StringName &p_name) const {
	const int *slot = names.getptr(p_name);
	return slot ? *slot : -1;
}

bool WShaderParameterLayout::compatible(int p_slot, const WShaderParameterLayout &p_other, int p_other_slot) const {
	const Entry &left = entries[p_slot];
	const Entry &right = p_other.entries[p_other_slot];
	return left.type == right.type && left.array == right.array && left.count == right.count;
}

void WShaderParameters::reset(const Ref<WShaderParameterLayout> &p_layout) {
	layout = p_layout;
	buffer = layout.is_valid() ? layout->defaults : WShaderParameterBuffer();
}

void WShaderParameters::remap(const Ref<WShaderParameterLayout> &p_layout) {
	if (layout == p_layout || p_layout.is_null()) {
		return;
	}
	WShaderParameters previous = *this;
	reset(p_layout);
	if (previous.layout.is_null() || layout.is_null()) {
		return;
	}
	for (int old_slot = 0; old_slot < previous.layout->entries.size(); old_slot++) {
		if (!previous.buffer.overridden[old_slot]) {
			continue;
		}
		const int slot = layout->find(previous.layout->entries[old_slot].name);
		if (slot < 0 || !layout->compatible(slot, *previous.layout.ptr(), old_slot)) {
			continue; // A changed shader declaration uses its new declared default.
		}
		WShaderParameterUpdate value = previous.update(old_slot);
		value.layout = layout;
		value.slot = slot;
		apply(value);
		const auto &entry = layout->entries[slot];
		if (buffer.colors[slot] && entry.texture < 0) {
			for (int element = 0; element < entry.count; element++) {
				const int offset = entry.offset + element * entry.stride;
				const Color color = WShaderPacking::Access<Color>::read(entry, buffer.words.ptr() + offset);
				WShaderPacking::Access<Color>::write(entry, buffer.linear_words.ptrw() + offset, color,
						entry.hint != ShaderLanguage::ShaderNode::Uniform::HINT_COLOR_CONVERSION_DISABLED);
			}
		}
	}
}

void WShaderParameters::clear(int p_slot) {
	ERR_FAIL_COND(layout.is_null());
	ERR_FAIL_INDEX(p_slot, layout->entries.size());
	WShaderParameters defaults;
	defaults.reset(layout);
	apply(defaults.update(p_slot));
}

WShaderParameterUpdate WShaderParameters::update(int p_slot) const {
	WShaderParameterUpdate result;
	ERR_FAIL_COND_V(layout.is_null(), result);
	ERR_FAIL_INDEX_V(p_slot, layout->entries.size(), result);
	const auto &entry = layout->entries[p_slot];
	result.layout = layout;
	result.slot = p_slot;
	result.overridden = buffer.overridden[p_slot];
	result.color = buffer.colors[p_slot];
	result.texture_count = buffer.texture_counts[p_slot];
	if (entry.texture >= 0) {
		if (entry.count == 1) {
			result.texture = buffer.textures[entry.texture];
		} else {
			result.array_textures = buffer.textures.slice(entry.texture, entry.texture + entry.count);
		}
	} else {
		const int count = entry.stride * entry.count;
		if (count <= 16) {
			memcpy(result.words, buffer.words.ptr() + entry.offset, count * sizeof(uint32_t));
			memcpy(result.linear_words, buffer.linear_words.ptr() + entry.offset, count * sizeof(uint32_t));
		} else {
			result.array_words = buffer.words.slice(entry.offset, entry.offset + count);
			result.array_linear_words = buffer.linear_words.slice(entry.offset, entry.offset + count);
		}
	}
	return result;
}

void WShaderParameters::apply(const WShaderParameterUpdate &p_update) {
	remap(p_update.layout);
	ERR_FAIL_COND(layout.is_null());
	ERR_FAIL_INDEX(p_update.slot, layout->entries.size());
	const auto &entry = layout->entries[p_update.slot];
	buffer.overridden.write[p_update.slot] = p_update.overridden;
	buffer.colors.write[p_update.slot] = p_update.color;
	buffer.texture_counts.write[p_update.slot] = p_update.texture_count;
	if (entry.texture >= 0) {
		if (entry.count == 1) {
			buffer.textures.write[entry.texture] = p_update.texture;
		} else {
			ERR_FAIL_COND(p_update.array_textures.size() != entry.count);
			memcpy(buffer.textures.ptrw() + entry.texture, p_update.array_textures.ptr(), entry.count * sizeof(RID));
		}
	} else {
		const int count = entry.stride * entry.count;
		ERR_FAIL_COND(count > 16 && (p_update.array_words.size() != count || p_update.array_linear_words.size() != count));
		memcpy(buffer.words.ptrw() + entry.offset, count <= 16 ? p_update.words : p_update.array_words.ptr(), count * sizeof(uint32_t));
		memcpy(buffer.linear_words.ptrw() + entry.offset, count <= 16 ? p_update.linear_words : p_update.array_linear_words.ptr(), count * sizeof(uint32_t));
	}
}

void WShaderParameters::copy_uniforms(uint8_t *p_buffer, uint32_t p_size, bool p_linear) const {
	const Vector<uint32_t> &words = p_linear ? buffer.linear_words : buffer.words;
	ERR_FAIL_COND(uint64_t(words.size()) * sizeof(uint32_t) != p_size);
	if (p_size > 0) {
		memcpy(p_buffer, words.ptr(), p_size);
	}
}

void WShaderParameters::get_textures(int p_texture_order, Vector<RID> &r_textures) const {
	ERR_FAIL_COND(layout.is_null());
	ERR_FAIL_INDEX(p_texture_order, layout->texture_entries.size());
	const int slot = layout->texture_entries[p_texture_order];
	ERR_FAIL_COND(slot < 0);
	const auto &entry = layout->entries[slot];
	r_textures.resize(entry.count);
	for (int index = 0; index < entry.count; index++) {
		const bool explicit_texture = buffer.overridden[slot] && index < buffer.texture_counts[slot];
		r_textures.write[index] = explicit_texture ? buffer.textures[entry.texture + index] : layout->defaults.textures[entry.texture + index];
	}
}

Ref<WShaderParameterLayout> WShaderParameterLayout::with_texture_default(const StringName &p_name, int p_index, RID p_texture) const {
	const int slot = find(p_name);
	if (slot < 0 || entries[slot].texture < 0 || p_index < 0 || p_index >= entries[slot].count) {
		return Ref<WShaderParameterLayout>(const_cast<WShaderParameterLayout *>(this));
	}
	Ref<WShaderParameterLayout> result = memnew(WShaderParameterLayout);
	result->entries = entries;
	result->texture_entries = texture_entries;
	result->names = names;
	result->defaults = defaults;
	result->defaults.textures.write[entries[slot].texture + p_index] = p_texture;
	return result;
}
