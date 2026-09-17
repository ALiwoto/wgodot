// wgodot-changes::file
#pragma once

#include "core/string/string_name.h"
#include "core/templates/vector.h"

class Object;

// Exported contracts have opaque type IDs. They describe compatibility, not
// C++ inheritance, and never create Script resources or script instances.
namespace WGodotNativeInterfaces {
void add_contract(const StringName &p_name, const StringName &p_native_base, const Vector<StringName> &p_parents);
void add_implementation(const StringName &p_contract, const StringName &p_class);
bool accepts(const StringName &p_class, const StringName &p_contract);
bool is_instance(Object *p_object, const StringName &p_type);
bool can_reference(const StringName &p_source, const StringName &p_target);
void clear();
} // namespace WGodotNativeInterfaces
