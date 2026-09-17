// wgodot-changes::file
#pragma once

#include "wgodot_native_dictionary_fwd.h"
#include "wgodot_native_warray.h"

#include "core/templates/hash_map.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant_caster.h"

#include <utility>

namespace WGodotNative {

template <class T>
struct DictionaryKeyHasher {
	static uint32_t hash(const T &p_key) {
		if constexpr (std::is_same_v<T, bool>) {
			return HashMapHasherDefault::hash(uint8_t(p_key));
		} else {
			return HashMapHasherDefault::hash(p_key);
		}
	}
};

// Copies share the map itself. Engine Dictionary/Variant storage is reached
// only by an explicit exporter boundary, never by an implicit conversion.
template <class K, class V>
class WDictionary {
	using Map = HashMap<K, V, DictionaryKeyHasher<K>>;
	struct Storage {
		SafeRefCount references;
		Map entries;
		bool read_only = false;

		Storage() { references.init(); }
	};
	Storage *storage;

	static void release(Storage *p_storage) {
		if (p_storage->references.unref()) {
			memdelete(p_storage);
		}
	}
	bool writable() const {
		ERR_FAIL_COND_V_MSG(storage->read_only, false, "WDictionary is read-only.");
		return true;
	}

public:
	using Key = K;
	using Value = V;

	WDictionary() : storage(memnew(Storage)) {}
	WDictionary(std::initializer_list<KeyValue<K, V>> p_entries) : WDictionary() {
		for (const auto &entry : p_entries) {
			storage->entries.insert(entry.key, entry.value);
		}
	}
	WDictionary(const WDictionary &p_other) : storage(p_other.storage) { storage->references.ref(); }
	~WDictionary() { release(storage); }
	WDictionary &operator=(const WDictionary &p_other) {
		Storage *previous = storage;
		p_other.storage->references.ref();
		storage = p_other.storage;
		release(previous);
		return *this;
	}

	int64_t size() const { return storage->entries.size(); }
	bool is_empty() const { return storage->entries.is_empty(); }
	bool is_read_only() const { return storage->read_only; }
	void make_read_only() { storage->read_only = true; }
	const Map &native() const { return storage->entries; }
	bool has(const K &p_key) const { return storage->entries.has(p_key); }
	const V *getptr(const K &p_key) const { return storage->entries.getptr(p_key); }
	V at(const K &p_key) const {
		const V *value = getptr(p_key);
		ERR_FAIL_NULL_V_MSG(value, V(), "Invalid native game dictionary key.");
		return *value;
	}
	V get(const K &p_key, V p_default = V()) const {
		const V *value = getptr(p_key);
		return value ? *value : p_default;
	}
	bool set(K p_key, V p_value) {
		if (!writable()) {
			return false;
		}
		storage->entries.insert(p_key, p_value);
		return true;
	}
	V get_or_add(K p_key, V p_default = V()) {
		if (const V *value = getptr(p_key)) {
			return *value;
		}
		set(p_key, p_default);
		return p_default;
	}
	bool erase(const K &p_key) { return writable() && storage->entries.erase(p_key); }
	void clear() {
		if (writable()) {
			storage->entries.clear();
		}
	}
	void sort() {
		if (writable()) {
			storage->entries.sort();
		}
	}
	void assign(const WDictionary &p_other) {
		if (writable() && storage != p_other.storage) {
			storage->entries = p_other.storage->entries;
		}
	}
	void assign_from_dictionary(const Variant &p_source) {
		ERR_FAIL_COND_MSG(p_source.get_type() != Variant::DICTIONARY, vformat("WDictionary.assign requires a Dictionary, got %s.", Variant::get_type_name(p_source.get_type())));
		if (!writable()) {
			return;
		}
		const Dictionary source = p_source;
		const ContainerTypeValidate targets[] = {
			{ GetTypeInfo<K>::VARIANT_TYPE, GetTypeInfo<K>::get_class_info().class_name, Ref<Script>(), "WDictionary.Key" },
			{ GetTypeInfo<V>::VARIANT_TYPE, GetTypeInfo<V>::get_class_info().class_name, Ref<Script>(), "WDictionary.Value" },
		};
		const ContainerType sources[] = { source.get_key_type(), source.get_value_type() };
		bool validate_entries[2];
		for (int i = 0; i < 2; i++) {
			const auto &target = targets[i];
			const ContainerTypeValidate source_type{ sources[i].builtin_type, sources[i].class_name, sources[i].script };
			// Match Dictionary.assign's handling of typed sources, including empty
			// dictionaries and object upcasts/downcasts. Untyped entries are checked below.
			validate_entries[i] = target != source_type && !(source_type.type == Variant::OBJECT && target.can_reference(source_type));
			const bool object_downcast = target.type == Variant::OBJECT && source_type.type == Variant::OBJECT && source_type.can_reference(target);
			ERR_FAIL_COND_MSG(validate_entries[i] && source_type.type != Variant::NIL && !object_downcast &&
					(target.type == Variant::OBJECT || source_type.type == Variant::OBJECT || !Variant::can_convert_strict(source_type.type, target.type)),
					vformat("WDictionary.assign cannot convert %s from %s to %s.", i == 0 ? "keys" : "values", Variant::get_type_name(source_type.type), Variant::get_type_name(target.type)));
		}

		Map entries(source.size());
		for (const auto &entry : source) {
			Variant key = entry.key;
			Variant value = entry.value;
			if ((validate_entries[0] && !targets[0].validate(key, "assign")) || (validate_entries[1] && !targets[1].validate(value, "assign"))) {
				return;
			}
			entries.insert(VariantCaster<K>::cast(key), VariantCaster<V>::cast(value));
		}
		// Keep the destination's identity and all aliases. Failed conversions leave
		// its contents intact; the engine dictionary never becomes its backing storage.
		storage->entries = std::move(entries);
	}
	void merge(const WDictionary &p_other, bool p_overwrite = false) {
		if (!writable() || storage == p_other.storage) {
			return;
		}
		for (const auto &entry : p_other.native()) {
			if (p_overwrite || !has(entry.key)) {
				storage->entries.insert(entry.key, entry.value);
			}
		}
	}
	WDictionary merged(const WDictionary &p_other, bool p_overwrite = false) const {
		WDictionary result = duplicate();
		result.merge(p_other, p_overwrite);
		return result;
	}
	WArray<K> keys() const {
		WArray<K> result;
		result.reserve(size());
		for (const auto &entry : native()) {
			result.append(entry.key);
		}
		return result;
	}
	WArray<V> values() const {
		WArray<V> result;
		result.reserve(size());
		for (const auto &entry : native()) {
			result.append(entry.value);
		}
		return result;
	}
	bool has_all(const WArray<K> &p_keys) const {
		for (int64_t i = 0; i < p_keys.size(); i++) {
			if (!has(p_keys.get(i))) {
				return false;
			}
		}
		return true;
	}
	const K *find_key(const V &p_value) const {
		for (const auto &entry : native()) {
			if (entry.value == p_value) {
				return &entry.key;
			}
		}
		return nullptr;
	}
	const K *next_key(const K *p_key = nullptr) const {
		auto iterator = p_key ? native().find(*p_key) : native().begin();
		if (p_key) {
			ERR_FAIL_COND_V_MSG(!iterator, nullptr, "Dictionary key erased during iteration.");
			++iterator;
		}
		return iterator ? &iterator->key : nullptr;
	}
	WDictionary duplicate(bool p_deep = false) const {
		WDictionary result;
		for (const auto &entry : native()) {
			V value = entry.value;
			if constexpr (IsWArray<V>::value || IsWDictionary<V>::value || std::is_base_of_v<Dictionary, V>) {
				if (p_deep) {
					value = value.duplicate(true);
				}
			}
			result.storage->entries.insert(entry.key, value);
		}
		return result;
	}
	Dictionary duplicate_to_dictionary(bool p_deep = false) const {
		Dictionary result;
		const PropertyInfo key = GetTypeInfo<K>::get_class_info();
		const PropertyInfo value = GetTypeInfo<V>::get_class_info();
		result.set_typed(GetTypeInfo<K>::VARIANT_TYPE, key.class_name, Variant(), GetTypeInfo<V>::VARIANT_TYPE, value.class_name, Variant());
		for (const auto &entry : native()) {
			result.set(entry.key, entry.value);
		}
		return p_deep ? result.duplicate(true) : result;
	}
	bool operator==(const WDictionary &p_other) const {
		if (storage == p_other.storage) {
			return true;
		}
		if (size() != p_other.size()) {
			return false;
		}
		for (const auto &entry : native()) {
			const V *value = p_other.getptr(entry.key);
			if (!value || !(entry.value == *value)) {
				return false;
			}
		}
		return true;
	}
	bool operator!=(const WDictionary &p_other) const { return !(*this == p_other); }
};

} // namespace WGodotNative
