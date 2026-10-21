// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/templates/local_vector.h"
#include <cstdlib>
#include <type_traits>

// Timing is shared by every value type. Values have exactly one concrete store;
// generic editor/VM access materializes a Variant only at that API boundary.
struct WAnimationKey {
	real_t transition = 1.0;
	double time = 0.0;
};

struct WAnimationSample {
	int from = -1;
	int next = -1;
	int pre = -1;
	int post = -1;
	real_t weight = 0;
	real_t pre_time = 0;
	real_t next_time = 0;
	real_t post_time = 0;
	int interpolation = 0;
};

// Mixed integer/float authoring keys retain their original numeric semantics:
// all-integer spans round, mixed spans interpolate as doubles. No Variant keys.
struct WAnimationNumber {
	union {
		int64_t integer;
		double real;
	};
	bool is_integer = false;
	constexpr WAnimationNumber() : real(0) {}
	constexpr explicit WAnimationNumber(int64_t p_value) : integer(p_value), is_integer(true) {}
	constexpr explicit WAnimationNumber(double p_value) : real(p_value) {}
	double as_double() const { return is_integer ? double(integer) : real; }
	Variant to_variant() const { return is_integer ? Variant(integer) : Variant(real); }
};

// One list for storage, native factories and mixer specialization. The enum is
// internal; serialized formats continue to use Godot's public Variant types.
#define W_ANIMATION_VALUE_TYPES(m) \
	m(Bool, bool, BOOL) \
	m(Int, int64_t, INT) \
	m(Float, double, FLOAT) \
	m(Vector2, Vector2, VECTOR2) \
	m(Vector3, Vector3, VECTOR3) \
	m(Vector4, Vector4, VECTOR4) \
	m(Vector2i, Vector2i, VECTOR2I) \
	m(Vector3i, Vector3i, VECTOR3I) \
	m(Vector4i, Vector4i, VECTOR4I) \
	m(Rect2, Rect2, RECT2) \
	m(Rect2i, Rect2i, RECT2I) \
	m(Quaternion, Quaternion, QUATERNION) \
	m(Color, Color, COLOR) \
	m(Plane, Plane, PLANE) \
	m(Aabb, AABB, AABB) \
	m(Basis, Basis, BASIS) \
	m(Transform2D, Transform2D, TRANSFORM2D) \
	m(Transform3D, Transform3D, TRANSFORM3D) \
	m(String, String, STRING) \
	m(StringName, StringName, STRING_NAME) \
	m(Vector2Array, PackedVector2Array, PACKED_VECTOR2_ARRAY) \
	m(Vector3Array, PackedVector3Array, PACKED_VECTOR3_ARRAY) \
	m(Vector4Array, PackedVector4Array, PACKED_VECTOR4_ARRAY) \
	m(ColorArray, PackedColorArray, PACKED_COLOR_ARRAY) \
	m(Float32Array, PackedFloat32Array, PACKED_FLOAT32_ARRAY) \
	m(Float64Array, PackedFloat64Array, PACKED_FLOAT64_ARRAY) \
	m(Int32Array, PackedInt32Array, PACKED_INT32_ARRAY) \
	m(Int64Array, PackedInt64Array, PACKED_INT64_ARRAY) \
	m(ByteArray, PackedByteArray, PACKED_BYTE_ARRAY) \
	m(StringArray, PackedStringArray, PACKED_STRING_ARRAY) \
	m(Resource, Ref<Resource>, OBJECT) \
	m(NodePath, NodePath, NODE_PATH) \
	m(Rid, RID, RID)

enum class WAnimationValueKind {
	Empty,
#define W_ANIMATION_KIND(m_name, m_type, m_variant) m_name,
	W_ANIMATION_VALUE_TYPES(W_ANIMATION_KIND)
#undef W_ANIMATION_KIND
	Number,
	Generic, // Heterogeneous editor data; rejected by native animation export.
};

template <typename T> struct WAnimationValueType;
#define W_ANIMATION_TYPE(m_name, m_type, m_variant) \
	template <> struct WAnimationValueType<m_type> { \
		static constexpr WAnimationValueKind kind = WAnimationValueKind::m_name; \
		static constexpr Variant::Type variant = Variant::m_variant; \
	};
W_ANIMATION_VALUE_TYPES(W_ANIMATION_TYPE)
#undef W_ANIMATION_TYPE
template <> struct WAnimationValueType<WAnimationNumber> {
	static constexpr WAnimationValueKind kind = WAnimationValueKind::Number;
	static constexpr Variant::Type variant = Variant::FLOAT;
};
template <> struct WAnimationValueType<Variant> {
	static constexpr WAnimationValueKind kind = WAnimationValueKind::Generic;
	static constexpr Variant::Type variant = Variant::NIL;
};

class WAnimationValues {
	struct Data {
		virtual ~Data() = default;
		virtual WAnimationValueKind kind() const = 0;
		virtual Variant get(uint32_t p_index) const = 0;
		virtual void set(uint32_t p_index, const Variant &p_value) = 0;
		virtual void resize(uint32_t p_size) = 0;
		virtual void insert(uint32_t p_index) = 0;
		virtual void remove(uint32_t p_index) = 0;
		virtual Data *clone() const = 0;
	};
	template <typename T> struct TypedData : Data {
		LocalVector<T> values;
		WAnimationValueKind kind() const override { return WAnimationValueType<T>::kind; }
		Variant get(uint32_t p_index) const override {
			if constexpr (std::is_same_v<T, WAnimationNumber>) {
				return values[p_index].to_variant();
			} else {
				return values[p_index];
			}
		}
		void set(uint32_t p_index, const Variant &p_value) override {
			if constexpr (std::is_same_v<T, WAnimationNumber>) {
				values[p_index] = p_value.get_type() == Variant::INT ? WAnimationNumber(int64_t(p_value)) : WAnimationNumber(double(p_value));
			} else {
				values[p_index] = p_value;
			}
		}
		void resize(uint32_t p_size) override { values.resize(p_size); }
		void insert(uint32_t p_index) override { values.insert(p_index, T()); }
		void remove(uint32_t p_index) override { values.remove_at(p_index); }
		Data *clone() const override {
			auto *copy = memnew(TypedData<T>);
			copy->values = values;
			return copy;
		}
	};
	Data *data = nullptr;
	// Optional outgoing segment timing. Empty for ordinary Animation tracks.
	// (x1,y1,x2,y2) is a normalized Bezier; (-1,0,0,0) is stepped.
	PackedVector4Array timing_curves;
	int timing_curve_subdivisions = 0;
	PackedVector2Array timing_samples;
	void rebuild_timing_samples();
	void ensure_type(const Variant &p_value);
	void change_type(Data *p_data);

public:
	LocalVector<WAnimationKey> keys;
	WAnimationValues() = default;
	WAnimationValues(const WAnimationValues &p_other);
	WAnimationValues(WAnimationValues &&p_other);
	WAnimationValues &operator=(const WAnimationValues &p_other);
	WAnimationValues &operator=(WAnimationValues &&p_other);
	~WAnimationValues();
	WAnimationValueKind kind() const { return data ? data->kind() : WAnimationValueKind::Empty; }
	uint32_t size() const { return keys.size(); }
	bool is_empty() const { return keys.is_empty(); }
	void clear() { *this = WAnimationValues(); }
	void resize(uint32_t p_size);
	void remove_at(uint32_t p_index);
	void insert(uint32_t p_index, const WAnimationKey &p_key, const Variant &p_value);
	Variant get(uint32_t p_index) const { return data ? data->get(p_index) : Variant(); }
	void set(uint32_t p_index, const Variant &p_value);
	Variant sample_variant(const WAnimationSample &p_sample) const;
	Error set_timing_curves(const PackedVector4Array &p_curves, int p_subdivisions = 0);
	const PackedVector4Array &get_timing_curves() const { return timing_curves; }
	int get_timing_curve_subdivisions() const { return timing_curve_subdivisions; }
	WAnimationSample ease_sample(WAnimationSample p_sample, bool p_backward) const;

	template <typename T> const LocalVector<T> &typed() const {
		DEV_ASSERT(kind() == WAnimationValueType<T>::kind);
		return static_cast<const TypedData<T> *>(data)->values;
	}
	template <typename T> void assign(LocalVector<WAnimationKey> &&p_keys, LocalVector<T> &&p_values) {
		DEV_ASSERT(p_keys.size() == p_values.size());
		auto *replacement = memnew(TypedData<T>);
		replacement->values = std::move(p_values);
		if (data) {
			memdelete(data);
		}
		data = replacement;
		keys = std::move(p_keys);
		timing_curves.clear();
		timing_curve_subdivisions = 0;
		timing_samples.clear();
	}

	// Dispatch once per track operation, never once per component/key.
	template <typename F> decltype(auto) visit(F &&p_function) const {
		switch (kind()) {
#define W_ANIMATION_VISIT(m_name, m_type, m_variant) \
			case WAnimationValueKind::m_name: return p_function(typed<m_type>());
			W_ANIMATION_VALUE_TYPES(W_ANIMATION_VISIT)
#undef W_ANIMATION_VISIT
			case WAnimationValueKind::Number: return p_function(typed<WAnimationNumber>());
			case WAnimationValueKind::Generic: return p_function(typed<Variant>());
			case WAnimationValueKind::Empty: {
				const LocalVector<Variant> empty;
				return p_function(empty);
			}
		}
		CRASH_NOW();
		std::abort(); // MSVC's debugger trap can return; invalid kinds must not.
	}
};
