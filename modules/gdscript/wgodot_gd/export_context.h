// wgodot-changes::file
/**************************************************************************/
/*  export_context.h                                                      */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "../gdscript_parser.h"

#include "core/math/random_pcg.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/typedefs.h"

namespace WGodotGDScriptExportTransform {

class ExportContext {
	TransformOptions options;
	HashMap<String, String> member_renames;
	HashMap<StringName, StringName> global_class_renames;
	HashMap<String, StringName> global_class_renames_by_path;
	HashMap<StringName, StringName> builtin_class_aliases;
	HashMap<StringName, StringName> builtin_function_aliases;
	HashMap<StringName, StringName> builtin_instance_method_aliases;
	HashMap<StringName, StringName> builtin_static_method_aliases;
	HashMap<StringName, StringName> builtin_instance_property_aliases;
	HashMap<StringName, StringName> builtin_static_property_aliases;
	HashMap<String, String> script_path_renames;
	HashMap<uint64_t, String> string_resources;
	HashMap<String, String> obfuscated_string_literals;
	HashSet<StringName> reserved_member_names;
	HashSet<StringName> reserved_global_class_names;
	HashSet<String> reserved_script_paths;
	HashSet<uint64_t> reserved_string_resource_ids;
	RandomPCG obfuscation_random;

	void reserve_registered_global_class_names();
	void reserve_builtin_class_names();
	void reserve_builtin_function_names();

public:
	void reset();
	void set_options(const TransformOptions &p_options);
	const TransformOptions &get_options() const;
	void reserve_member_name(const StringName &p_name);
	void reserve_global_class_name(const StringName &p_name);
	void reserve_script_path(const String &p_path);
	void reserve_script_global_class_name(const GDScriptParser::ClassNode *p_class);
	void reserve_script_declaration_names_for_global_classes(const GDScriptParser::ClassNode *p_class);
	void seed_reserved_obfuscated_names(HashSet<StringName> &r_reserved_names) const;
	String make_obfuscated_name(HashSet<StringName> &r_reserved_names, const String &p_warning_context);

	static String make_member_key(const String &p_class_key, const StringName &p_member_name);
	static void make_member_keys(const GDScriptParser::ClassNode *p_class, const String &p_script_path, const StringName &p_member_name, Vector<String> &r_keys);

	void index_script(const GDScriptParser::ClassNode *p_class, const String &p_script_path);
	void index_global_class_rename(const GDScriptParser::ClassNode *p_class, const String &p_script_path);
	String get_or_create_member_rename(const String &p_key);
	void bind_member_rename(const String &p_key, const String &p_obfuscated_name);
	const String *get_member_rename(const String &p_key) const;
	StringName get_or_create_global_class_rename(const StringName &p_name, const String &p_path);
	const StringName *get_global_class_rename(const StringName &p_name) const;
	const StringName *get_global_class_rename_by_path(const String &p_path) const;
	String get_or_create_script_path_rename(const String &p_path);
	const String *get_script_path_rename(const String &p_path) const;
	String get_exported_script_path(const String &p_path) const;
	uint32_t get_random_uint(uint32_t p_bounds);
	uint64_t create_string_resource(const String &p_value);
	const HashMap<uint64_t, String> &get_string_resources() const;
	String get_or_create_obfuscated_string_literal(Variant::Type p_type, const String &p_value);
	StringName get_or_create_builtin_class_alias(const StringName &p_name);
	const StringName *get_builtin_class_alias(const StringName &p_name) const;
	const HashMap<StringName, StringName> &get_builtin_class_aliases() const;
	StringName get_or_create_builtin_function_alias(const StringName &p_name);
	const StringName *get_builtin_function_alias(const StringName &p_name) const;
	const HashMap<StringName, StringName> &get_builtin_function_aliases() const;
	StringName get_or_create_builtin_member_alias(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property);
	const StringName *get_builtin_member_alias(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) const;
	const HashMap<StringName, StringName> &get_builtin_instance_method_aliases() const;
	const HashMap<StringName, StringName> &get_builtin_static_method_aliases() const;
	const HashMap<StringName, StringName> &get_builtin_instance_property_aliases() const;
	const HashMap<StringName, StringName> &get_builtin_static_property_aliases() const;
};

} // namespace WGodotGDScriptExportTransform
