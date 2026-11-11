// wgodot-changes::file
#pragma once

#include "core/object/object.h"

namespace WGodotNative {

// Used only for slots which generated initialization definitely assigns before
// script code can read them. It is not an empty container value.
struct DeferredInitialization {};

// The supplied-field mask lives on the constructing thread's stack. Objects
// retain neither the mask nor a construction/immutability flag after loading.
class ConstructionFields {
	inline static thread_local const ConstructionFields *current = nullptr;
	const ConstructionFields *previous;
	const Object *object;
	const bool *fields;

public:
	ConstructionFields(const Object *p_object, const bool *p_fields) : previous(current), object(p_object), fields(p_fields) {
		current = this;
	}
	~ConstructionFields() {
		current = previous;
	}
	ConstructionFields(const ConstructionFields &) = delete;
	ConstructionFields &operator=(const ConstructionFields &) = delete;

	static const bool *for_object(const Object *p_object) {
		return current && current->object == p_object ? current->fields : nullptr;
	}
};

} // namespace WGodotNative
