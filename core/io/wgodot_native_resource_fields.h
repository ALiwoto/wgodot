// wgodot-changes::file
#pragma once

#include "core/object/object.h"

// Native keyed serialization (e.g. theme items), without constructing an
// instance or accepting arbitrary Variant properties.
class WGodotNativeResourceFields {
public:
	using Resolver = bool (*)(const String &, PropertyInfo &);
	struct Dependency {
		StringName property;
		String prefix;
		void (*declarations)(const Variant &, List<PropertyInfo> &) = nullptr;
	};
	static void register_type(const StringName &p_class, Resolver p_resolver);
	static void register_dependency(const StringName &p_class, const Dependency &p_dependency);
	static void unregister_type(const StringName &p_class);
	static bool field(const StringName &p_class, const String &p_name, PropertyInfo &r_info);
	static Dependency dependency(const StringName &p_class);
};
