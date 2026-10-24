// wgodot-changes::file
#pragma once

#include "wgodot_shader_parameters.h"

#include "core/os/mutex.h"
#include "core/variant/type_info.h"

// Instance uniforms occupy one four-word GPU slot. Keep their CPU values in
// that ABI too; Variant is used only by the public scripting/Inspector bridge.
struct WInstanceShaderValue {
	ShaderLanguage::DataType type = ShaderLanguage::TYPE_VOID;
	Variant::Type source_type = Variant::NIL;
	uint32_t words[4] = {};
	uint32_t linear_words[4] = {};

	bool valid() const { return type != ShaderLanguage::TYPE_VOID; }
	static WInstanceShaderValue from_uniform(const ShaderLanguage::ShaderNode::Uniform &p_uniform);
	static WInstanceShaderValue from_variant(const Variant &p_value);
	Variant to_variant() const;
	bool convert_to(ShaderLanguage::DataType p_type);

	template <typename T> static WInstanceShaderValue pack(const T &p_value) {
		WInstanceShaderValue result;
		result.source_type = GetTypeInfo<T>::VARIANT_TYPE;
		using SL = ShaderLanguage;
		if constexpr (std::is_same_v<T, bool>) {
			result.type = SL::TYPE_BOOL;
		} else if constexpr (std::is_integral_v<T>) {
			result.type = SL::TYPE_INT;
		} else if constexpr (std::is_floating_point_v<T>) {
			result.type = SL::TYPE_FLOAT;
		} else if constexpr (std::is_same_v<T, Vector2>) {
			result.type = SL::TYPE_VEC2;
		} else if constexpr (std::is_same_v<T, Vector3>) {
			result.type = SL::TYPE_VEC3;
		} else if constexpr (std::is_same_v<T, Vector4> || std::is_same_v<T, Color>) {
			result.type = SL::TYPE_VEC4;
		} else if constexpr (std::is_same_v<T, Vector2i>) {
			result.type = SL::TYPE_IVEC2;
		} else if constexpr (std::is_same_v<T, Vector3i>) {
			result.type = SL::TYPE_IVEC3;
		} else if constexpr (std::is_same_v<T, Vector4i>) {
			result.type = SL::TYPE_IVEC4;
		} else {
			static_assert(!sizeof(T), "Unsupported instance-uniform value type");
		}
		WShaderParameterLayout::Entry entry;
		entry.type = result.type;
		WShaderPacking::Access<T>::write(entry, result.words, p_value, false);
		WShaderPacking::Access<T>::write(entry, result.linear_words, p_value, true);
		return result;
	}

	template <typename T> T unpack() const {
		WShaderParameterLayout::Entry entry;
		entry.type = type;
		if constexpr (std::is_arithmetic_v<T>) {
			if (type == ShaderLanguage::TYPE_FLOAT) {
				return T(WShaderPacking::read_float(words[0]));
			}
			if (type == ShaderLanguage::TYPE_BOOL) {
				return T(words[0] != 0);
			}
			if (WShaderPacking::Access<int64_t>::accepts(entry)) {
				return T(WShaderPacking::Access<int64_t>::read(entry, words));
			}
		}
		ERR_FAIL_COND_V_MSG(!WShaderPacking::Access<T>::accepts(entry), T(), "Instance-uniform type does not match the requested C++ type.");
		return WShaderPacking::Access<T>::read(entry, words);
	}
};

// One bank per render instance. Slots never move, including across material
// changes. The node and renderer share this authoritative CPU state. A short
// lock protects metadata/value publication; render commands own their values.
class WInstanceShaderParameters : public RefCounted {
public:
	enum class SetResult {
		INVALID,
		UNCHANGED,
		CHANGED,
	};

	struct Item {
		PropertyInfo info;
		int gpu_index = -1;
		WInstanceShaderValue value;
		WInstanceShaderValue default_value;
		bool overridden = false;
	};

private:
	mutable Mutex mutex;
	HashMap<StringName, int> names;
	Vector<Item> items;

public:
	int find_or_add(const StringName &p_name);
	int find(const StringName &p_name) const;
	void invalidate();
	void define(const PropertyInfo &p_info, int p_gpu_index, const WInstanceShaderValue &p_default);
	Vector<Item> snapshot() const;
	Item get(int p_slot) const;
	WInstanceShaderValue value(int p_slot) const;
	SetResult set(int p_slot, WInstanceShaderValue &r_value);
	WInstanceShaderValue clear(int p_slot);
	int gpu_index(int p_slot, WInstanceShaderValue &r_value) const;
};
