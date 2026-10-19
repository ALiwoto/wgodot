// wgodot-changes::file
#pragma once

#include "servers/rendering/storage/variant_converters.h"

#include <type_traits>

namespace WShaderPacking {
using SL = ShaderLanguage;
using Entry = WShaderParameterLayout::Entry;

inline float read_float(uint32_t p_word) {
	float result;
	memcpy(&result, &p_word, sizeof(result));
	return result;
}

inline void write_float(uint32_t &r_word, float p_value) {
	memcpy(&r_word, &p_value, sizeof(p_value));
}

inline int components(SL::DataType p_type) {
	switch (p_type) {
		case SL::TYPE_BOOL:
		case SL::TYPE_INT:
		case SL::TYPE_UINT:
		case SL::TYPE_FLOAT:
			return 1;
		case SL::TYPE_BVEC2:
		case SL::TYPE_IVEC2:
		case SL::TYPE_UVEC2:
		case SL::TYPE_VEC2:
			return 2;
		case SL::TYPE_BVEC3:
		case SL::TYPE_IVEC3:
		case SL::TYPE_UVEC3:
		case SL::TYPE_VEC3:
			return 3;
		case SL::TYPE_BVEC4:
		case SL::TYPE_IVEC4:
		case SL::TYPE_UVEC4:
		case SL::TYPE_VEC4:
			return 4;
		case SL::TYPE_MAT2:
			return 4;
		case SL::TYPE_MAT3:
			return 9;
		case SL::TYPE_MAT4:
			return 16;
		default:
			return 0;
	}
}

inline int matrix_columns(SL::DataType p_type) {
	switch (p_type) {
		case SL::TYPE_MAT2:
			return 2;
		case SL::TYPE_MAT3:
			return 3;
		case SL::TYPE_MAT4:
			return 4;
		default:
			return 0;
	}
}

inline int word_index(SL::DataType p_type, int p_component) {
	const int columns = matrix_columns(p_type);
	return columns ? (p_component / columns) * 4 + p_component % columns : p_component;
}

inline bool integer(SL::DataType p_type) {
	return p_type >= SL::TYPE_INT && p_type <= SL::TYPE_UVEC4;
}

inline bool boolean(SL::DataType p_type) {
	return p_type >= SL::TYPE_BOOL && p_type <= SL::TYPE_BVEC4;
}

template <typename T>
struct Access;

template <>
struct Access<bool> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_BOOL; }
	static void write(const Entry &, uint32_t *p_words, bool p_value, bool) { p_words[0] = p_value ? 1 : 0; }
	static bool read(const Entry &, const uint32_t *p_words) { return p_words[0] != 0; }
};

template <>
struct Access<int64_t> {
	static bool accepts(const Entry &p_entry) {
		return p_entry.type == SL::TYPE_INT || p_entry.type == SL::TYPE_UINT || boolean(p_entry.type);
	}
	static void write(const Entry &p_entry, uint32_t *p_words, int64_t p_value, bool) {
		if (boolean(p_entry.type)) {
			for (int i = 0; i < components(p_entry.type); i++) {
				p_words[i] = (uint64_t(p_value) & (uint64_t(1) << i)) ? 1 : 0;
			}
		} else {
			p_words[0] = uint32_t(p_value);
		}
	}
	static int64_t read(const Entry &p_entry, const uint32_t *p_words) {
		if (boolean(p_entry.type)) {
			int64_t result = 0;
			for (int i = 0; i < components(p_entry.type); i++) {
				result |= int64_t(p_words[i] != 0) << i;
			}
			return result;
		}
		return p_entry.type == SL::TYPE_UINT ? int64_t(p_words[0]) : int64_t(int32_t(p_words[0]));
	}
};

template <>
struct Access<int32_t> : Access<int64_t> {};

template <>
struct Access<double> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_FLOAT; }
	static void write(const Entry &, uint32_t *p_words, double p_value, bool) { write_float(p_words[0], float(p_value)); }
	static double read(const Entry &, const uint32_t *p_words) { return read_float(p_words[0]); }
};

template <>
struct Access<float> : Access<double> {};

template <typename T, SL::DataType Type>
struct VectorAccess {
	static bool accepts(const Entry &p_entry) { return p_entry.type == Type; }
	static void write(const Entry &, uint32_t *p_words, const T &p_value, bool) {
		float packed[T::AXIS_COUNT];
		VariantConverterStd140<T>::convert(p_value, packed, false);
		memcpy(p_words, packed, sizeof(packed));
	}
	static T read(const Entry &, const uint32_t *p_words) {
		T result;
		for (int i = 0; i < T::AXIS_COUNT; i++) {
			result[i] = read_float(p_words[i]);
		}
		return result;
	}
};

template <> struct Access<Vector2> : VectorAccess<Vector2, SL::TYPE_VEC2> {};
template <> struct Access<Vector3> : VectorAccess<Vector3, SL::TYPE_VEC3> {};
template <> struct Access<Vector4> : VectorAccess<Vector4, SL::TYPE_VEC4> {};

template <typename T, SL::DataType SignedType, SL::DataType UnsignedType>
struct IntegerVectorAccess {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SignedType || p_entry.type == UnsignedType; }
	static void write(const Entry &, uint32_t *p_words, const T &p_value, bool) {
		for (int i = 0; i < T::AXIS_COUNT; i++) {
			p_words[i] = uint32_t(p_value[i]);
		}
	}
	static T read(const Entry &, const uint32_t *p_words) {
		T result;
		for (int i = 0; i < T::AXIS_COUNT; i++) {
			result[i] = int32_t(p_words[i]);
		}
		return result;
	}
};

template <> struct Access<Vector2i> : IntegerVectorAccess<Vector2i, SL::TYPE_IVEC2, SL::TYPE_UVEC2> {};
template <> struct Access<Vector3i> : IntegerVectorAccess<Vector3i, SL::TYPE_IVEC3, SL::TYPE_UVEC3> {};
template <> struct Access<Vector4i> : IntegerVectorAccess<Vector4i, SL::TYPE_IVEC4, SL::TYPE_UVEC4> {};

template <>
struct Access<Color> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_VEC3 || p_entry.type == SL::TYPE_VEC4; }
	static void write(const Entry &p_entry, uint32_t *p_words, const Color &p_value, bool p_linear) {
		const Color value = p_linear ? p_value.srgb_to_linear() : p_value;
		for (int i = 0; i < components(p_entry.type); i++) {
			write_float(p_words[i], value[i]);
		}
	}
	static Color read(const Entry &p_entry, const uint32_t *p_words) {
		return Color(read_float(p_words[0]), read_float(p_words[1]), read_float(p_words[2]),
				p_entry.type == SL::TYPE_VEC4 ? read_float(p_words[3]) : 1.0f);
	}
};

template <>
struct Access<Transform2D> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_MAT2; }
	static void write(const Entry &, uint32_t *p_words, const Transform2D &p_value, bool) {
		// mat2 has two padded columns, not Transform2D's third translation column.
		for (int column = 0; column < 2; column++) {
			for (int row = 0; row < 2; row++) {
				write_float(p_words[column * 4 + row], p_value.columns[column][row]);
			}
		}
	}
	static Transform2D read(const Entry &, const uint32_t *p_words) {
		return Transform2D(Vector2(read_float(p_words[0]), read_float(p_words[1])),
				Vector2(read_float(p_words[4]), read_float(p_words[5])), Vector2());
	}
};

template <>
struct Access<Basis> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_MAT3; }
	static void write(const Entry &, uint32_t *p_words, const Basis &p_value, bool) {
		float packed[12];
		VariantConverterStd140<Basis>::convert(p_value, packed, false);
		memcpy(p_words, packed, sizeof(packed));
	}
	static Basis read(const Entry &, const uint32_t *p_words) {
		Basis result;
		for (int column = 0; column < 3; column++) {
			for (int row = 0; row < 3; row++) {
				result.rows[row][column] = read_float(p_words[column * 4 + row]);
			}
		}
		return result;
	}
};

template <>
struct Access<Projection> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_MAT4; }
	static void write(const Entry &, uint32_t *p_words, const Projection &p_value, bool) {
		float packed[16];
		VariantConverterStd140<Projection>::convert(p_value, packed, false);
		memcpy(p_words, packed, sizeof(packed));
	}
	static Projection read(const Entry &, const uint32_t *p_words) {
		Projection result;
		for (int column = 0; column < 4; column++) {
			for (int row = 0; row < 4; row++) {
				result.columns[column][row] = read_float(p_words[column * 4 + row]);
			}
		}
		return result;
	}
};

template <>
struct Access<Transform3D> {
	static bool accepts(const Entry &p_entry) { return p_entry.type == SL::TYPE_MAT4; }
	static void write(const Entry &, uint32_t *p_words, const Transform3D &p_value, bool) {
		float packed[16];
		VariantConverterStd140<Transform3D>::convert(p_value, packed, false);
		memcpy(p_words, packed, sizeof(packed));
	}
	static Transform3D read(const Entry &p_entry, const uint32_t *p_words) {
		return Access<Projection>::read(p_entry, p_words);
	}
};

template <typename T>
struct IsArray : std::false_type {};
template <typename T>
struct IsArray<Vector<T>> : std::true_type { using Element = T; };

template <typename T>
constexpr bool is_color = std::is_same_v<T, Color> || std::is_same_v<T, Vector<Color>>;

template <typename T>
bool accepts(const Entry &p_entry) {
	if constexpr (IsArray<T>::value) {
		using Element = typename IsArray<T>::Element;
		if constexpr (std::is_same_v<Element, float> || std::is_same_v<Element, double>) {
			return p_entry.array && p_entry.type >= SL::TYPE_FLOAT && p_entry.type <= SL::TYPE_MAT4;
		} else if constexpr (std::is_same_v<Element, int32_t>) {
			return p_entry.array && (integer(p_entry.type) || boolean(p_entry.type));
		} else {
			return p_entry.array && Access<Element>::accepts(p_entry);
		}
	} else {
		return !p_entry.array && Access<T>::accepts(p_entry);
	}
}

template <typename T>
void write(const Entry &p_entry, uint32_t *p_words, const T &p_value, bool p_linear) {
	if constexpr (IsArray<T>::value) {
		using Element = typename IsArray<T>::Element;
		for (int i = 0; i < p_entry.count; i++) {
			uint32_t *target = p_words + i * p_entry.stride;
			if constexpr (std::is_arithmetic_v<Element>) {
				const int width = components(p_entry.type);
				const int columns = matrix_columns(p_entry.type);
				for (int component = 0; component < width; component++) {
					const int index = i * width + component;
					const Element value = (i + 1) * width <= p_value.size() ? p_value[index] : Element(columns && component / columns == component % columns);
					uint32_t &word = target[word_index(p_entry.type, component)];
					if constexpr (std::is_floating_point_v<Element>) {
						write_float(word, float(value));
					} else {
						word = boolean(p_entry.type) ? uint32_t(value != 0) : uint32_t(value);
					}
				}
			} else {
				if constexpr (std::is_same_v<Element, Color>) {
					// Missing vec3/vec4 array elements are zero, including alpha.
					Access<Element>::write(p_entry, target, i < p_value.size() ? p_value[i] : Color(0, 0, 0, 0), p_linear);
				} else {
					Access<Element>::write(p_entry, target, i < p_value.size() ? p_value[i] : Element(), p_linear);
				}
			}
		}
	} else {
		Access<T>::write(p_entry, p_words, p_value, p_linear);
	}
}

template <typename T>
T read(const Entry &p_entry, const uint32_t *p_words) {
	if constexpr (IsArray<T>::value) {
		using Element = typename IsArray<T>::Element;
		T result;
		const int width = std::is_arithmetic_v<Element> ? components(p_entry.type) : 1;
		result.resize(p_entry.count * width);
		Element *target = result.ptrw();
		for (int i = 0; i < p_entry.count; i++) {
			const uint32_t *source = p_words + i * p_entry.stride;
			if constexpr (std::is_arithmetic_v<Element>) {
				for (int component = 0; component < width; component++) {
					const uint32_t word = source[word_index(p_entry.type, component)];
					if constexpr (std::is_floating_point_v<Element>) {
						target[i * width + component] = read_float(word);
					} else {
						target[i * width + component] = int32_t(word);
					}
				}
			} else {
				target[i] = Access<Element>::read(p_entry, source);
			}
		}
		return result;
	} else {
		return Access<T>::read(p_entry, p_words);
	}
}
} // namespace WShaderPacking

template <typename T>
bool WShaderParameters::set(int p_slot, const T &p_value) {
	ERR_FAIL_COND_V(layout.is_null(), false);
	ERR_FAIL_INDEX_V(p_slot, layout->entries.size(), false);
	const auto &entry = layout->entries[p_slot];
	ERR_FAIL_COND_V_MSG(!WShaderPacking::accepts<T>(entry), false, "Shader parameter type mismatch: " + entry.name);
	WShaderPacking::write(entry, buffer.words.ptrw() + entry.offset, p_value, false);
	WShaderPacking::write(entry, buffer.linear_words.ptrw() + entry.offset, p_value,
			entry.hint != ShaderLanguage::ShaderNode::Uniform::HINT_COLOR_CONVERSION_DISABLED);
	buffer.overridden.write[p_slot] = 1;
	buffer.colors.write[p_slot] = WShaderPacking::is_color<T>;
	return true;
}

template <typename T>
T WShaderParameters::get(int p_slot) const {
	ERR_FAIL_COND_V(layout.is_null(), T());
	ERR_FAIL_INDEX_V(p_slot, layout->entries.size(), T());
	const auto &entry = layout->entries[p_slot];
	ERR_FAIL_COND_V_MSG(!WShaderPacking::accepts<T>(entry), T(), "Shader parameter type mismatch: " + entry.name);
	return WShaderPacking::read<T>(entry, buffer.words.ptr() + entry.offset);
}
