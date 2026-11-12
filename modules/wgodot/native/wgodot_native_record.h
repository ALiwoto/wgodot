// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"
#include "wgodot_native_static.h"
#include "wgodot_native_value.h"
#include "wgodot_native_warray.h"
#include "wgodot_native_wdictionary.h"

#include "core/variant/wgodot_record.h"

namespace WGodotNative {

template <class T>
bool record_matches(const Variant &p_value) {
	if (p_value.get_type() != Variant::WRECORD) {
		return false;
	}
	const WRecord record = p_value;
	return !record.is_empty() && record.schema()->get_name() == T::record_name();
}

template <class T>
WRecordFieldType record_field_type(bool p_value = false) {
	WRecordFieldType type;
	type.value_container = p_value;
	if constexpr (is_record<T>) {
		type.builtin = Variant::WRECORD;
		type.record_name = T::record_name();
	} else if constexpr (IsWArray<T>::value) {
		type.builtin = Variant::ARRAY;
		type.elements.push_back(record_field_type<typename T::Element>());
	} else if constexpr (IsWDictionary<T>::value) {
		type.builtin = Variant::DICTIONARY;
		type.elements.push_back(record_field_type<typename T::Key>());
		type.elements.push_back(record_field_type<typename T::Value>());
	} else if constexpr (IsPacked<T>::value) {
		type.builtin = T::KIND;
	} else {
		type.builtin = GetTypeInfo<T>::VARIANT_TYPE;
		type.class_name = GetTypeInfo<T>::get_class_info().class_name;
		if constexpr (std::is_same_v<T, float>) {
			type.scalar = WGodotScalar::FLOAT32;
		} else if constexpr (std::is_same_v<T, int32_t>) {
			type.scalar = WGodotScalar::INT32;
		} else if constexpr (std::is_same_v<T, uint8_t>) {
			type.scalar = WGodotScalar::UINT8;
		}
	}
	return type;
}

// Explicit record/Variant boundaries recursively adapt native containers. The
// typed fields remain native values throughout ordinary gameplay execution.
template <bool Literal = false, class T>
Variant record_variant(const T &p_value) {
	if constexpr (Literal && is_record<T>) {
		Dictionary result;
		T::visit_fields(p_value, [&](const StringName &p_name, const auto &p_field, bool) {
			result[p_name] = record_variant<true>(p_field);
		});
		return result;
	} else if constexpr (IsWArray<T>::value) {
		Array result;
		if constexpr (!Literal) {
			const WRecordFieldType element = record_field_type<typename T::Element>();
			result.set_typed(element.builtin, element.record_name.is_empty() ? element.class_name : element.record_name, Variant());
		}
		result.reserve(p_value.size());
		for (int index = 0; index < p_value.size(); ++index) {
			result.push_back(record_variant<Literal>(p_value.get(index)));
		}
		if (p_value.is_value()) {
			result.wgodot_make_value();
		}
		return result;
	} else if constexpr (IsWDictionary<T>::value) {
		Dictionary result;
		if constexpr (!Literal) {
			const WRecordFieldType key = record_field_type<typename T::Key>();
			const WRecordFieldType value = record_field_type<typename T::Value>();
			result.set_typed(key.builtin, key.record_name.is_empty() ? key.class_name : key.record_name, Variant(),
					value.builtin, value.record_name.is_empty() ? value.class_name : value.record_name, Variant());
		}
		for (const auto &entry : p_value.native()) {
			result[record_variant<Literal>(entry.key)] = record_variant<Literal>(entry.value);
		}
		if (p_value.is_value()) {
			result.wgodot_make_value();
		}
		return result;
	} else {
		return Variant(p_value);
	}
}

template <class T>
T record_value(const Variant &p_value) {
	if constexpr (IsWArray<T>::value) {
		const Array source = p_value;
		typename T::Native values;
		values.reserve(source.size());
		for (int index = 0; index < source.size(); ++index) {
			values.push_back(record_value<typename T::Element>(source[index]));
		}
		T result = T::from_native(values);
		if (source.wgodot_is_value()) {
			result.make_value();
		}
		return result;
	} else if constexpr (IsWDictionary<T>::value) {
		const Dictionary source = p_value;
		T result;
		for (const auto &entry : source) {
			result.set(record_value<typename T::Key>(entry.key), record_value<typename T::Value>(entry.value));
		}
		if (source.wgodot_is_value()) {
			result.make_value();
		}
		return result;
	} else {
		return convert<T>(p_value);
	}
}

template <class T>
bool record_field_equal(const T &p_left, const T &p_right) {
	if constexpr (is_record<T>) {
		return p_left == p_right;
	} else if constexpr (IsWArray<T>::value || IsPacked<T>::value) {
		if (p_left.size() != p_right.size()) {
			return false;
		}
		for (int index = 0; index < p_left.size(); ++index) {
			if (!record_field_equal(p_left.get(index), p_right.get(index))) {
				return false;
			}
		}
		return true;
	} else if constexpr (IsWDictionary<T>::value) {
		if (p_left.size() != p_right.size()) {
			return false;
		}
		for (const auto &entry : p_left.native()) {
			const auto *value = p_right.getptr(entry.key);
			if (!value || !record_field_equal(entry.value, *value)) {
				return false;
			}
		}
		return true;
	} else {
		return Variant(p_left).hash_compare(Variant(p_right));
	}
}

template <class T>
struct RecordSchemaStorage {
	Ref<WRecordSchema> schema;
	static void initialize(RecordSchemaStorage &r_storage) {
		Vector<WRecordSchema::Field> fields;
		Vector<Variant> defaults;
		const T initial;
		T::visit_fields(initial, [&](const StringName &p_name, const auto &p_value, bool p_container) {
			fields.push_back({ p_name, record_field_type<std::decay_t<decltype(p_value)>>(p_container) });
			// Literal trees must not re-enter schema initialization through
			// record-to-Variant conversion of a recursive default's children.
			defaults.push_back(record_variant<true>(p_value));
		});
		r_storage.schema.instantiate();
		r_storage.schema->initialize(T::record_name(), String(), fields, defaults);
		WRecordSchemas::publish(r_storage.schema);
	}
};

template <class T>
uint32_t record_field_hash(const T &p_value, int p_depth) {
	ERR_FAIL_COND_V(p_depth > Variant::MAX_RECURSION_DEPTH, 0);
	if constexpr (is_record<T>) {
		return p_value.record_hash(p_depth);
	} else if constexpr (IsWArray<T>::value) {
		uint32_t hash = hash_murmur3_one_32(Variant::ARRAY);
		for (const auto &value : p_value.native()) {
			hash = hash_murmur3_one_32(record_field_hash(value, p_depth + 1), hash);
		}
		return hash_fmix32(hash);
	} else if constexpr (IsWDictionary<T>::value) {
		uint32_t hash = hash_murmur3_one_32(Variant::DICTIONARY);
		for (const auto &entry : p_value.native()) {
			hash += hash_murmur3_one_32(record_field_hash(entry.value, p_depth + 1), record_field_hash(entry.key, p_depth + 1));
		}
		return hash_fmix32(hash);
	} else {
		return Variant(p_value).recursive_hash(p_depth);
	}
}

template <class T>
struct RecordValue : RecordTag {
	static const Ref<WRecordSchema> &record_schema() {
		return StaticStorage<RecordSchemaStorage<T>>::get(&RecordSchemaStorage<T>::initialize).schema;
	}
	operator Variant() const {
		const Ref<WRecordSchema> &schema = record_schema();
		Vector<Variant> fields;
		fields.reserve(schema->field_count());
		T::visit_fields(static_cast<const T &>(*this), [&](const StringName &, const auto &p_value, bool) {
			fields.push_back(record_variant(p_value));
		});
		return WRecord(schema, fields);
	}
	bool read_record(const Variant &p_value) {
		if (p_value.get_type() != Variant::WRECORD) {
			return false;
		}
		const WRecord source = p_value;
		if (source.is_empty() || source.schema() != record_schema()) {
			return false;
		}
		int index = 0;
		T::visit_fields(static_cast<T &>(*this), [&](const StringName &, auto &r_value, bool) {
			r_value = record_value<std::decay_t<decltype(r_value)>>(source.get(index++));
		});
		return true;
	}
	T duplicate_record(ResourceDeepDuplicateMode p_mode, int p_depth) const {
		T result = static_cast<const T &>(*this);
		ERR_FAIL_COND_V(p_depth > Variant::MAX_RECURSION_DEPTH, result);
		ContainerDuplicateScope scope(p_depth);
		T::visit_fields(result, [&](const StringName &, auto &r_value, bool) {
			r_value = duplicate_container_value(r_value, p_mode, p_depth + 1);
		});
		return result;
	}
	uint32_t record_hash(int p_depth) const {
		ERR_FAIL_COND_V(p_depth > Variant::MAX_RECURSION_DEPTH, 0);
		uint32_t hash = hash_murmur3_one_32(T::record_name().hash());
		T::visit_fields(static_cast<const T &>(*this), [&](const StringName &, const auto &p_value, bool) {
			hash = hash_murmur3_one_32(record_field_hash(p_value, p_depth + 1), hash);
		});
		return hash_fmix32(hash);
	}
	uint32_t hash() const { return record_hash(0); }
};

} // namespace WGodotNative

template <class T>
struct GetTypeInfo<T, std::enable_if_t<WGodotNative::is_record<T>>> {
	static constexpr Variant::Type VARIANT_TYPE = Variant::WRECORD;
	static constexpr GodotTypeInfo::Metadata METADATA = GodotTypeInfo::METADATA_NONE;
	static PropertyInfo get_class_info() { return PropertyInfo(Variant::WRECORD, String(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT, T::record_name()); }
};
