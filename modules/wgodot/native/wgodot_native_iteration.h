// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"

#include "core/variant/wgodot_array_access.h"

namespace WGodotNative {

template <class T, bool IsObject = std::is_base_of_v<Object, T>>
struct ArrayElement {
	using Owned = T;
	static Owned read(const Variant &p_slot) { return VariantInternalAccessor<T>::get(&p_slot); }
};

template <class T>
struct ArrayElement<T, true> {
	using Owned = typename ObjectView<T>::Owned;
	static Owned read(const Variant &p_slot) { return ObjectView<T>(p_slot).owned(); }
};

// The exporter establishes the element type from the engine's typed return
// contract. Retain the original array, including sharing and changes to size.
template <class T, bool ViewObjects = true>
class ArrayRange {
	// Object itself can hold RefCounted instances, even though it is not derived
	// from RefCounted. Both require a retained current element.
	static constexpr bool can_borrow = ViewObjects && std::is_base_of_v<Object, T> && !std::is_base_of_v<RefCounted, T> && !std::is_base_of_v<T, RefCounted>;
	bool borrow_slots = false;
	Array collection;

public:
	ArrayRange() = default;
	explicit ArrayRange(const Array &p_array, bool p_temporary = false) :
			borrow_slots(p_temporary && can_borrow && !array_is_shared(p_array)), collection(p_array) {}
	typename ArrayElement<T>::Owned read(int64_t p_index) const { return ArrayElement<T>::read(collection[p_index]); }
	int64_t size() const { return collection.size(); }
	ArrayRange<T, false> owned() const { return ArrayRange<T, false>(collection); }
	struct Sentinel {};
	class Iterator {
		const ArrayRange *range;
		int64_t position = 0;
		Variant current;

	public:
		explicit Iterator(const ArrayRange *p_range) : range(p_range) {}
		auto operator*() {
			if constexpr (std::is_base_of_v<Object, T> && ViewObjects) {
				if (range->borrow_slots) {
					return ObjectView<T>(range->collection[position]);
				}
				// A shared array may replace or remove this slot during the body.
				// RefCounted loop values also require their own retained reference.
				current = range->collection[position];
				return ObjectView<T>(current);
			} else {
				return range->read(position);
			}
		}
		Iterator &operator++() {
			++position;
			return *this;
		}
		bool operator!=(Sentinel) const { return position < range->size(); }
	};
	Iterator begin() const { return Iterator(this); }
	Sentinel end() const { return {}; }
};

template <class T>
ArrayRange<T> iterate(const Array &p_array) {
	return ArrayRange<T>(p_array);
}

template <class T>
ArrayRange<T> iterate(Array &&p_array) {
	// An lvalue owner remains accessible to the loop even if it was unique.
	// Only a sole temporary owner can provide stable borrowed slots.
	return ArrayRange<T>(p_array, true);
}

// Coroutines keep the collection and index in the frame. Their loop variable
// already has owning storage, so no borrowed view crosses a suspension point.
template <class T>
class ArrayIterator {
	ArrayRange<T> collection;
	int64_t position = 0;

public:
	explicit ArrayIterator(const ArrayRange<T> &p_collection) : collection(p_collection) {}
	bool has_value() const { return position < collection.size(); }
	void next() { position++; }
	template <class Result>
	Result get() const { return convert<Result>(collection.read(position)); }
};

} // namespace WGodotNative
