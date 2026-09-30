// wgodot-changes::file
#pragma once

#if defined(WGODOT_NATIVE_GAME) && defined(DEBUG_ENABLED)
#include <type_traits>

class Object;

namespace WGodotNativeAllocation {
// Independent of Object's header: memory.h is also used while defining Object.
inline void (*object_allocated)(Object *, const char *) = nullptr;

template <class T>
void record_type(T *p_object) {
	if constexpr (std::is_convertible_v<T *, Object *>) {
		if (object_allocated) {
			// The compiler's signature names T, including internal subclasses
			// without GDCLASS. This is a string literal, not RTTI.
#ifdef _MSC_VER
			object_allocated(p_object, __FUNCSIG__);
#else
			object_allocated(p_object, __PRETTY_FUNCTION__);
#endif
		}
	}
}
} // namespace WGodotNativeAllocation
#endif
