// wgodot-changes::file
#pragma once

#include "core/io/file_access.h"
#include "core/object/script_language.h"

namespace WGodotNamedTypes {
Ref<Script> script(const StringName &p_name);
StringName native_type(const StringName &p_name);
StringName type_name(const Object *p_object);
class Resolver {
	HashMap<StringName, Ref<Script>> scripts;

public:
	Ref<Script> script(const StringName &p_name);
	Error initialize_resource(const Ref<Resource> &p_resource, const StringName &p_name);
};
void dependencies(const Ref<FileAccess> &p_file, List<String> *r_dependencies, bool p_add_types);
bool is_script_reference(const String &p_type, const String &p_path);
} // namespace WGodotNamedTypes
