// wgodot-changes::file
#pragma once

#include "wgodot_native_callback_fwd.h"

#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/type_info.h"
#include "core/variant/variant.h"

#include <type_traits>

namespace WGodotNative {

// Handles share one vector object, including its size and current allocation.
// There is deliberately no implicit Array/Variant conversion or MethodBind ABI.
template <class T>
class WArray {
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
	const Vector<T> &native() const { return storage->elements; }

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
	void sort() {
		if (writable()) {
			storage->elements.sort();
		}
	}
	template <class Compare>
	void sort_custom(const Compare &p_compare) {
		struct Comparator {
			Compare callback;
			explicit Comparator(Compare p_callback) : callback(std::move(p_callback)) {}
			bool operator()(const T &p_left, const T &p_right) const { return callback.call(p_left, p_right); }
		};
		if (writable()) {
			storage->elements.template sort_custom<Comparator, true>(p_compare);
		}
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

	WArray duplicate(bool p_deep = false) const {
		WArray result;
		result.storage->elements = storage->elements;
		// Godot's deep Array copy recursively copies nested Arrays/Dictionaries;
		// objects and the other value types retain their normal copy semantics.
		if constexpr (std::is_same_v<T, Dictionary> || std::is_base_of_v<Dictionary, T>) {
			if (p_deep) {
				for (int64_t i = 0; i < size(); i++) {
					result.storage->elements.set(i, T(storage->elements[i].duplicate(true)));
				}
			}
		}
		return result;
	}

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

template <class T>
struct IsWArray : std::false_type {};
template <class T>
struct IsWArray<WArray<T>> : std::true_type {};

} // namespace WGodotNative
