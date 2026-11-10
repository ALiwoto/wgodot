// wgodot-changes::file
#pragma once

#include "wgodot_native_serialization.h"
#include "wgodot_native_static.h"

#include "core/io/wgodot_global_resources.h"

namespace WGodotNative {
template <class T>
struct GlobalValues {
	Mutex mutex;
	HashMap<StringName, HashMap<StringName, T>> values;
};

template <class T>
T global_value(const StringName &p_resource, const StringName &p_member) {
	if constexpr (IsWArray<T>::value || IsWDictionary<T>::value) {
		// Native typed containers have different storage from Variant containers.
		// Convert each explicitly shared value once per native type, not per user.
		auto &cache = StaticStorage<GlobalValues<T>>::get();
		MutexLock lock(cache.mutex);
		auto &members = cache.values[p_resource];
		if (const T *found = members.getptr(p_member)) {
			return *found;
		}
		T result = deserialize_value<T>(WGlobalResources::value(p_resource, p_member));
		members.insert(p_member, result);
		return result;
	} else {
		// Packed arrays and resources retain the original shared storage directly.
		return convert<T>(WGlobalResources::value(p_resource, p_member));
	}
}
} // namespace WGodotNative
