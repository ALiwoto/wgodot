// wgodot-changes::file
#pragma once

#include "core/object/object.h"

// Native keyed serialization (e.g. theme items), without constructing an
// instance or accepting arbitrary Variant properties.
class WGodotNativeResourceFields {
public:
	using Resolver = bool (*)(const String &, PropertyInfo &);
	static void register_type(const StringName &p_class, Resolver p_resolver);
	static void unregister_type(const StringName &p_class);
	static bool field(const StringName &p_class, const String &p_name, PropertyInfo &r_info);
};
