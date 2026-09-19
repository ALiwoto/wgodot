// wgodot-changes::file
#pragma once

#include "wgodot_native_array_fwd.h"
#include "wgodot_native_callback_fwd.h"
#include "wgodot_native_container_ops.h"
#include "wgodot_native_dictionary_fwd.h"

#include "core/math/math_funcs.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/type_info.h"
#include "core/variant/variant.h"

#include <climits>
#include <type_traits>
#include <utility>

namespace WGodotNative {

// Handles share one vector object, including its size and current allocation.
// There is deliberately no implicit Array/Variant conversion or MethodBind ABI.
template <class T>
class WArray {
	static_assert(!std::is_same_v<T, Variant>, "WArray requires a concrete element type.");
	template <class>
	friend class WArray;
	template <class Compare>
	struct CallbackComparator {
		Compare callback;
		explicit CallbackComparator(Compare p_callback) : callback(std::move(p_callback)) {}
		bool operator()(const T &p_left, const T &p_right) const { return bool(callback.call(p_left, p_right)); }
	};
	struct Storage {
		SafeRefCount references;
		Vector<T> elements;
		bool read_only = false;

		Storage() { references.init(); }
		explicit Storage(std::initializer_list<T> p_values) : elements(p_values) { references.init(); }
	};

	Storage *storage;

	static void release(Storage *p_storage) {
		if (p_storage->references.unref()) {
			memdelete(p_storage);
		}
	}

	bool writable() const {
		ERR_FAIL_COND_V_MSG(storage->read_only, false, "WArray is read-only.");
		return true;
	}

	int64_t index(int64_t p_index) const { return p_index < 0 ? p_index + size() : p_index; }

public:
	using Element = T;

	WArray() : storage(memnew(Storage)) {}
	WArray(std::initializer_list<T> p_values) : storage(memnew(Storage(p_values))) {}
	WArray(const WArray &p_other) : storage(p_other.storage) { storage->references.ref(); }
	~WArray() { release(storage); }

	WArray &operator=(const WArray &p_other) {
		Storage *previous = storage;
		p_other.storage->references.ref();
		storage = p_other.storage;
		release(previous);
		return *this;
	}

	int64_t size() const { return storage->elements.size(); }
	bool is_empty() const { return storage->elements.is_empty(); }
	bool is_read_only() const { return storage->read_only; }
	void make_read_only() { storage->read_only = true; }
	// native-array: internal
	const Vector<T> &native() const { return storage->elements; }
	bool is_typed() const { return true; }
	template <class U>
	bool is_same_typed(const WArray<U> &) const { return std::is_same_v<T, U>; }
	uint32_t get_typed_builtin() const {
		if constexpr (IsWArray<T>::value) {
			return Variant::ARRAY;
		} else if constexpr (IsWDictionary<T>::value) {
			return Variant::DICTIONARY;
		} else if constexpr (IsWCallable<T>::value) {
			return Variant::CALLABLE;
		} else if constexpr (IsWSignal<T>::value) {
			return Variant::SIGNAL;
		} else {
			return GetTypeInfo<T>::VARIANT_TYPE;
		}
	}
	StringName get_typed_class_name() const {
		if constexpr (IsWArray<T>::value || IsWDictionary<T>::value || IsWCallable<T>::value || IsWSignal<T>::value) {
			return StringName();
		} else {
			return GetTypeInfo<T>::get_class_info().class_name;
		}
	}
	// Native game classes have no corresponding GDScript resource.
	// native-array: unsupported=Script resources are unavailable in native games
	std::nullptr_t get_typed_script() const = delete;
	uint32_t hash() const {
		uint32_t result = hash_murmur3_one_32(Variant::ARRAY);
		for (const T &value : storage->elements) {
			result = hash_murmur3_one_32(container_value_hash(value), result);
		}
		return hash_fmix32(result);
	}

	T get(int64_t p_index) const {
		const int64_t position = index(p_index);
		ERR_FAIL_INDEX_V(position, size(), T());
		return storage->elements[position];
	}

	void set(int64_t p_index, T p_value) {
		if (!writable()) {
			return;
		}
		const int64_t position = index(p_index);
		ERR_FAIL_INDEX(position, size());
		storage->elements.set(position, p_value);
	}

	void clear() {
		if (writable()) {
			storage->elements.clear();
		}
	}
	void append(T p_value) {
		if (writable()) {
			storage->elements.push_back(p_value);
		}
	}
	void push_back(T p_value) { append(std::move(p_value)); }
	Error insert(int64_t p_position, T p_value) {
		if (!writable()) {
			return ERR_LOCKED;
		}
		return storage->elements.insert(index(p_position), p_value);
	}
	void push_front(T p_value) { insert(0, p_value); }
	Error resize(int64_t p_size) {
		return writable() ? storage->elements.resize_initialized(p_size) : ERR_LOCKED;
	}
	Error reserve(int64_t p_size) { return writable() ? storage->elements.reserve(p_size) : ERR_LOCKED; }
	void remove_at(int64_t p_index) {
		if (writable()) {
			storage->elements.remove_at(index(p_index));
		}
	}
	T front() const { return get(0); }
	T back() const { return get(-1); }
	T pick_random() const {
		ERR_FAIL_COND_V_MSG(is_empty(), T(), "Can't take a value from an empty WArray.");
		return storage->elements[Math::rand() % size()];
	}
	T pop_at(int64_t p_index) {
		if (!writable() || is_empty()) {
			return T();
		}
		const int64_t position = index(p_index);
		ERR_FAIL_INDEX_V(position, size(), T());
		T value = storage->elements[position];
		storage->elements.remove_at(position);
		return value;
	}
	T pop_back() { return pop_at(-1); }
	T pop_front() { return pop_at(0); }
	int64_t find(const T &p_value, int64_t p_from = 0) const { return p_from < 0 ? -1 : storage->elements.find(p_value, p_from); }
	int64_t rfind(const T &p_value, int64_t p_from = -1) const {
		const int64_t from = index(p_from);
		return storage->elements.rfind(p_value, from < 0 || from >= size() ? size() - 1 : from);
	}
	template <class Predicate>
	int64_t find_custom(const Predicate &p_predicate, int64_t p_from = 0) const {
		if (p_from < 0) {
			return -1;
		}
		for (int64_t i = p_from; i < size(); i++) {
			if (p_predicate.call(get(i))) {
				return i;
			}
		}
		return -1;
	}
	template <class Predicate>
	int64_t rfind_custom(const Predicate &p_predicate, int64_t p_from = -1) const {
		const int64_t from = index(p_from);
		for (int64_t i = from < 0 || from >= size() ? size() - 1 : from; i >= 0; i--) {
			if (p_predicate.call(get(i))) {
				return i;
			}
		}
		return -1;
	}
	int64_t count(const T &p_value) const { return storage->elements.count(p_value); }
	bool has(const T &p_value) const { return find(p_value) >= 0; }
	void erase(const T &p_value) {
		if (writable()) {
			storage->elements.erase(p_value);
		}
	}
	void reverse() {
		if (writable()) {
			storage->elements.reverse();
		}
	}
	// native-array: ordered
	void sort() {
		if (writable()) {
			storage->elements.sort();
		}
	}
	template <class Compare>
	void sort_custom(const Compare &p_compare) {
		if (writable()) {
			storage->elements.template sort_custom<CallbackComparator<Compare>, true>(CallbackComparator<Compare>(p_compare));
		}
	}
	void shuffle() {
		if (!writable() || size() < 2) {
			return;
		}
		T *elements = storage->elements.ptrw();
		for (int64_t i = size() - 1; i > 0; i--) {
			const int64_t other = Math::rand() % (i + 1);
			SWAP(elements[i], elements[other]);
		}
	}
	// native-array: ordered
	int64_t bsearch(const T &p_value, bool p_before = true) const { return storage->elements.bsearch(p_value, p_before); }
	template <class Compare>
	int64_t bsearch_custom(const T &p_value, const Compare &p_compare, bool p_before = true) const {
		return storage->elements.template bsearch_custom<CallbackComparator<Compare>>(p_value, p_before, p_compare);
	}
	// native-array: ordered
	T min() const {
		ERR_FAIL_COND_V_MSG(is_empty(), T(), "An empty WArray has no minimum value.");
		T result = get(0);
		for (int64_t i = 1; i < size(); i++) {
			if (storage->elements[i] < result) {
				result = storage->elements[i];
			}
		}
		return result;
	}
	// native-array: ordered
	T max() const {
		ERR_FAIL_COND_V_MSG(is_empty(), T(), "An empty WArray has no maximum value.");
		T result = get(0);
		for (int64_t i = 1; i < size(); i++) {
			if (result < storage->elements[i]) {
				result = storage->elements[i];
			}
		}
		return result;
	}
	void fill(T p_value) {
		if (writable()) {
			storage->elements.fill(p_value);
		}
	}
	void append_array(const WArray &p_other) {
		if (writable()) {
			storage->elements.append_array(p_other.storage->elements);
		}
	}
	void assign(const WArray &p_other) {
		if (writable()) {
			storage->elements = p_other.storage->elements;
		}
	}

	WArray slice(int64_t p_begin, int64_t p_end = INT_MAX, int64_t p_step = 1, bool p_deep = false) const {
		WArray result;
		ERR_FAIL_COND_V_MSG(p_step == 0, result, "Slice step cannot be zero.");
		const int64_t length = size();
		if (length == 0 || (p_begin < -length && p_step < 0) || (p_begin >= length && p_step > 0)) {
			return result;
		}
		int64_t begin = CLAMP(p_begin, -length, length - 1);
		if (begin < 0) {
			begin += length;
		}
		int64_t end = CLAMP(p_end, -length - 1, length);
		if (end < 0) {
			end += length;
		}
		ERR_FAIL_COND_V_MSG((p_step > 0 && begin > end) || (p_step < 0 && begin < end), result, "Slice step and bounds have opposite directions.");
		const int64_t count = (end - begin) / p_step + ((end - begin) % p_step != 0);
		result.resize(count);
		for (int64_t i = 0, position = begin; i < count; i++) {
			result.storage->elements.set(i, p_deep ? duplicate_container_value(get(position), RESOURCE_DEEP_DUPLICATE_NONE, 1) : get(position));
			if (i + 1 < count) {
				position += p_step;
			}
		}
		return result;
	}
	template <class Predicate>
	WArray filter(const Predicate &p_predicate) const {
		WArray result;
		result.reserve(size());
		for (int64_t i = 0; i < size(); i++) {
			if (p_predicate.call(get(i))) {
				result.append(get(i));
			}
		}
		return result;
	}
	template <class Mapper>
	auto map(const Mapper &p_callback) const -> WArray<std::decay_t<decltype(p_callback.call(std::declval<T>()))>> {
		using U = std::decay_t<decltype(p_callback.call(std::declval<T>()))>;
		static_assert(!std::is_same_v<U, Variant> && !std::is_void_v<U>, "WArray.map requires a concrete result type.");
		WArray<U> result;
		result.reserve(size());
		for (int64_t i = 0; i < size(); i++) {
			result.append(p_callback.call(get(i)));
		}
		return result;
	}
	template <class Reducer, class Accumulator>
	Accumulator reduce(const Reducer &p_callback, Accumulator p_accumulator) const {
		for (int64_t i = 0; i < size(); i++) {
			p_accumulator = p_callback.call(p_accumulator, get(i));
		}
		return p_accumulator;
	}
	template <class Reducer>
	T reduce(const Reducer &p_callback) const {
		ERR_FAIL_COND_V_MSG(is_empty(), T(), "Reducing an empty WArray requires an initial accumulator.");
		T result = get(0);
		for (int64_t i = 1; i < size(); i++) {
			result = p_callback.call(result, get(i));
		}
		return result;
	}
	template <class Predicate>
	bool any(const Predicate &p_predicate) const {
		return find_custom(p_predicate) >= 0;
	}
	template <class Predicate>
	bool all(const Predicate &p_predicate) const {
		for (int64_t i = 0; i < size(); i++) {
			if (!p_predicate.call(get(i))) {
				return false;
			}
		}
		return true;
	}

	// native-array: copy=duplicate_to_array
	WArray duplicate(bool p_deep = false) const { return recursive_duplicate(p_deep, RESOURCE_DEEP_DUPLICATE_NONE, 0); }
	WArray duplicate_deep(int p_mode = RESOURCE_DEEP_DUPLICATE_INTERNAL) const {
		ERR_FAIL_INDEX_V(p_mode, RESOURCE_DEEP_DUPLICATE_MAX, WArray());
		return recursive_duplicate(true, ResourceDeepDuplicateMode(p_mode), 0);
	}
	// native-array: internal
	WArray recursive_duplicate(bool p_deep, ResourceDeepDuplicateMode p_mode, int p_depth) const {
		WArray result;
		ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, result, "Maximum container copy recursion reached.");
		result.storage->elements = storage->elements;
		if constexpr (container_needs_deep_copy<T>) {
			if (p_deep) {
				ContainerDuplicateScope scope(p_depth);
				for (int64_t i = 0; i < size(); i++) {
					result.storage->elements.set(i, duplicate_container_value(storage->elements[i], p_mode, p_depth + 1));
				}
			}
		}
		return result;
	}

	// native-array: internal
	Array duplicate_to_array(bool p_deep = false) const {
		Array result;
		if constexpr (IsWCallable<T>::value) {
			result.set_typed(Variant::CALLABLE, StringName(), Variant());
			result.resize(size());
			for (int64_t i = 0; i < size(); i++) {
				result[i] = storage->elements[i].to_callable();
			}
		} else {
			const PropertyInfo element = GetTypeInfo<T>::get_class_info();
			result.set_typed(GetTypeInfo<T>::VARIANT_TYPE, element.class_name, Variant());
			result.resize(size());
			for (int64_t i = 0; i < size(); i++) {
				result[i] = Variant(storage->elements[i]);
			}
		}
		return p_deep ? result.duplicate(true) : result;
	}

	bool operator==(const WArray &p_other) const {
		if (storage == p_other.storage) {
			return true;
		}
		if (size() != p_other.size()) {
			return false;
		}
		for (int64_t i = 0; i < size(); i++) {
			if (!(storage->elements[i] == p_other.storage->elements[i])) {
				return false;
			}
		}
		return true;
	}
	bool operator!=(const WArray &p_other) const { return !(*this == p_other); }
	WArray operator+(const WArray &p_other) const {
		WArray result = duplicate();
		result.append_array(p_other);
		return result;
	}
};

} // namespace WGodotNative
