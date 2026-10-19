// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"
#include "servers/rendering/shader_language.h"

// ShaderCompiler owns offsets and array strides. These buffers contain the
// shader ABI's 32-bit words, never the host layout of a C++ math structure.
struct WShaderParameterBuffer {
	Vector<uint32_t> words;
	Vector<uint32_t> linear_words;
	Vector<RID> textures;
	Vector<int> texture_counts;
	Vector<uint8_t> overridden;
	Vector<uint8_t> colors;
};

class WShaderParameterLayout : public RefCounted {
public:
	using Uniform = ShaderLanguage::ShaderNode::Uniform;
	struct Entry {
		StringName name;
		ShaderLanguage::DataType type = ShaderLanguage::TYPE_VOID;
		Uniform::Hint hint = Uniform::HINT_NONE;
		Variant::Type value_type = Variant::NIL;
		int offset = 0;
		int stride = 0;
		int count = 1;
		int texture = -1;
		bool array = false;
		bool valid() const { return type != ShaderLanguage::TYPE_VOID; }
	};

	Vector<Entry> entries;
	Vector<int> texture_entries;
	HashMap<StringName, int> names;
	WShaderParameterBuffer defaults;

	static Ref<WShaderParameterLayout> create(const HashMap<StringName, Uniform> &p_uniforms, const Vector<uint32_t> &p_offsets, uint32_t p_size);
	Ref<WShaderParameterLayout> with_texture_default(const StringName &p_name, int p_index, RID p_texture) const;
	int find(const StringName &p_name) const;
	bool compatible(int p_slot, const WShaderParameterLayout &p_other, int p_other_slot) const;
};

// A command owns its payload. Small updates (including mat4) do not allocate;
// arrays use COW vectors. No pointer into a mutable material crosses threads.
struct WShaderParameterUpdate {
	Ref<WShaderParameterLayout> layout;
	int slot = -1;
	uint32_t words[16] = {};
	uint32_t linear_words[16] = {};
	Vector<uint32_t> array_words;
	Vector<uint32_t> array_linear_words;
	RID texture;
	int texture_count = 0;
	Vector<RID> array_textures;
	bool overridden = false;
	bool color = false;
};

class WShaderParameters {
public:
	Ref<WShaderParameterLayout> layout;
	WShaderParameterBuffer buffer;

	void reset(const Ref<WShaderParameterLayout> &p_layout);
	void remap(const Ref<WShaderParameterLayout> &p_layout);
	void clear(int p_slot);
	WShaderParameterUpdate update(int p_slot) const;
	void apply(const WShaderParameterUpdate &p_update);
	void copy_uniforms(uint8_t *p_buffer, uint32_t p_size, bool p_linear) const;
	void get_textures(int p_texture_order, Vector<RID> &r_textures) const;

	template <typename T>
	bool set(int p_slot, const T &p_value);
	template <typename T>
	T get(int p_slot) const;
};

// Named/Variant APIs remain at editor and serialization boundaries. Native
// materials use the indexed buffer; regular engine materials retain their API.
struct WMaterialParameters {
	HashMap<StringName, Variant> named;
	WShaderParameters typed;
};

#include "wgodot_shader_parameter_access.h"
