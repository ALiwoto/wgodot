// wgodot-changes::file
#pragma once

#include "core/object/ref_counted.h"

// Native exports retain constructor metadata instead of VM Script objects.
// Source-mode references keep their existing language class objects.
class WNativeClassReference : public RefCounted {
	GDCLASS(WNativeClassReference, RefCounted);

public:
	using Constructor = Variant (*)(const StringName &);

private:
	StringName name;
	Constructor constructor = nullptr;

protected:
	static void _bind_methods() {}

public:
	void initialize(const StringName &p_name, Constructor p_constructor);
	const StringName &get_name() const { return name; }
	bool can_instantiate() const { return constructor != nullptr; }
	Variant instantiate() const;
};

namespace WGodotClassReferences {
struct Language {
	Variant (*resolve)(const StringName &) = nullptr;
	StringName (*name)(const Object *) = nullptr;
	StringName (*native_base)(const Object *) = nullptr;
};

// Registration occurs during language/module startup and shutdown, before or
// after resource workers. Resolving/inspecting references never mutates hooks.
void set_language(const Language &p_language);
void register_native(const StringName &p_name, WNativeClassReference::Constructor p_constructor);
void clear();

Variant resolve(const StringName &p_name);
StringName name(const Variant &p_reference);
StringName native_base(const Variant &p_reference);
bool accepts(const Variant &p_reference, const Variant &p_base);
bool accepts(const Variant &p_reference, const StringName &p_base);

// Typed-container metadata stores a ClassRef constraint alongside its Object
// element type; it is not the class of an ordinary object instance.
StringName constraint(const StringName &p_base);
StringName constraint_base(const StringName &p_constraint);
} // namespace WGodotClassReferences
