// wgodot-changes::file
#pragma once

#include "wgodot_native_array_fwd.h"
#include "wgodot_native_dictionary_fwd.h"
#include "wgodot_native_object.h"

#include "core/io/resource.h"
#include "core/variant/dictionary.h"

#include <utility>

namespace WGodotNative {

// Keep one resource remap session across a whole native container copy.
struct ContainerDuplicateScope {
	bool root;
	explicit ContainerDuplicateScope(int p_depth) : root(p_depth == 0) {}
	~ContainerDuplicateScope() {
		if (root) {
			Resource::_teardown_duplicate_from_variant();
		}
	}
};

template <class T, class = void>
struct IsContainerObject : std::false_type {};

template <class T>
struct IsContainerObject<T, std::void_t<decltype(std::declval<const T &>().ptr())>> :
		std::is_convertible<decltype(std::declval<const T &>().ptr()), Object *> {};

template <class T>
inline constexpr bool container_needs_deep_copy = IsWArray<T>::value || IsWDictionary<T>::value ||
		std::is_same_v<T, Array> || std::is_same_v<T, Dictionary> || IsContainerObject<T>::value;

template <class T>
T duplicate_container_value(const T &p_value, ResourceDeepDuplicateMode p_mode, int p_depth) {
	if constexpr (IsWArray<T>::value || IsWDictionary<T>::value || std::is_same_v<T, Array> || std::is_same_v<T, Dictionary>) {
		return p_value.recursive_duplicate(true, p_mode, p_depth);
	} else if constexpr (IsContainerObject<T>::value) {
		if (p_mode != RESOURCE_DEEP_DUPLICATE_NONE) {
			if (Resource *resource = Object::cast_to<Resource>(p_value.ptr())) {
				if (p_mode == RESOURCE_DEEP_DUPLICATE_ALL || resource->is_built_in()) {
					using ObjectType = std::remove_pointer_t<decltype(p_value.ptr())>;
					return T(Object::cast_to<ObjectType>(resource->_duplicate_from_variant(true, p_mode, p_depth).ptr()));
				}
			}
		}
	}
	return p_value;
}

// These math types have no HashMapHasherDefault specialization.
inline uint32_t container_value_hash(const Plane &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 3; i++) {
		hash = hash_murmur3_one_real(p_value.normal[i], hash);
	}
	return hash_fmix32(hash_murmur3_one_real(p_value.d, hash));
}
inline uint32_t container_value_hash(const Quaternion &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 4; i++) {
		hash = hash_murmur3_one_real(p_value[i], hash);
	}
	return hash_fmix32(hash);
}
inline uint32_t container_value_hash(const Transform2D &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 2; j++) {
			hash = hash_murmur3_one_real(p_value[i][j], hash);
		}
	}
	return hash_fmix32(hash);
}
inline uint32_t container_value_hash(const Basis &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			hash = hash_murmur3_one_real(p_value[i][j], hash);
		}
	}
	return hash_fmix32(hash);
}
inline uint32_t container_value_hash(const Transform3D &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			hash = hash_murmur3_one_real(p_value.basis[i][j], hash);
		}
	}
	for (int i = 0; i < 3; i++) {
		hash = hash_murmur3_one_real(p_value.origin[i], hash);
	}
	return hash_fmix32(hash);
}
inline uint32_t container_value_hash(const Projection &p_value) {
	uint32_t hash = HASH_MURMUR3_SEED;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			hash = hash_murmur3_one_real(p_value[i][j], hash);
		}
	}
	return hash_fmix32(hash);
}

template <class T>
uint32_t container_value_hash(const T &p_value) {
	if constexpr (std::is_same_v<T, bool>) {
		return p_value ? 1 : 0;
	} else if constexpr (std::is_enum_v<T>) {
		return hash_one_uint64(uint64_t(p_value));
	} else if constexpr (IsWDictionary<T>::value) {
		// Dictionary equality ignores insertion order.
		uint32_t hash = 0;
		for (const auto &entry : p_value.native()) {
			hash += hash_murmur3_one_32(container_value_hash(entry.value), container_value_hash(entry.key));
		}
		return hash_fmix32(hash);
	} else {
		return HashMapHasherDefault::hash(p_value);
	}
}

} // namespace WGodotNative
