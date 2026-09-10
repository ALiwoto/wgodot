// wgodot-changes::file

#pragma once

#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#ifdef TOOLS_ENABLED
#include "modules/gdscript/gdscript_cache.h"
#endif

namespace WGodotGDScriptResolution {

// Editor lookups can use an export revision. Templates call the normal services directly.
#ifdef TOOLS_ENABLED
bool is_global_class(const StringName &p_name);
String get_global_class_path(const StringName &p_name);
StringName get_global_class_native_base(const StringName &p_name);
Ref<Script> load_script(const String &p_path, const String &p_type = String());
bool resource_exists(const String &p_path);
String get_resource_type(const String &p_path);

bool is_export_analysis();
bool get_parser_override(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, Ref<GDScriptParserRef> &r_parser);
bool has_parser_override(const String &p_path, bool &r_exists);
bool get_shallow_script_override(const String &p_path, Error &r_error, Ref<GDScript> &r_script);
Ref<GDScriptParserRef> get_global_class_parser_override(const StringName &p_name);
bool resolve_native_alias_override(const StringName &p_name, StringName &r_name);
bool resolve_function_alias_override(const StringName &p_name, StringName &r_name);
bool resolve_member_alias_override(const StringName &p_name, bool p_static, bool p_property, StringName &r_name);
bool resolve_interface_alias_override(int p_interface_index, int p_method_index, StringName &r_name);
const HashMap<uint64_t, String> *get_string_resources_override();
#else
inline bool is_global_class(const StringName &p_name) {
	return ScriptServer::is_global_class(p_name);
}

inline String get_global_class_path(const StringName &p_name) {
	return ScriptServer::get_global_class_path(p_name);
}

inline StringName get_global_class_native_base(const StringName &p_name) {
	return ScriptServer::get_global_class_native_base(p_name);
}

inline Ref<Script> load_script(const String &p_path, const String &p_type = String()) {
	return ResourceLoader::load(p_path, p_type);
}

inline bool resource_exists(const String &p_path) {
	return ResourceLoader::exists(p_path);
}

inline String get_resource_type(const String &p_path) {
	return ResourceLoader::get_resource_type(p_path);
}

#endif

} // namespace WGodotGDScriptResolution
