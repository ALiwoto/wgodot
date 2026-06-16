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

namespace WGodotGDScriptExportTransform {

class ExportContext {
	TransformOptions options;
	HashMap<String, String> member_renames;
	HashMap<StringName, StringName> global_class_renames;
	HashMap<String, StringName> global_class_renames_by_path;
	HashMap<StringName, StringName> builtin_class_aliases;
	HashSet<StringName> reserved_member_names;
	HashSet<StringName> reserved_global_class_names;
	RandomPCG obfuscation_random;

	void reserve_registered_global_class_names();
	void reserve_builtin_class_names();

public:
	void reset();
	void set_options(const TransformOptions &p_options);
	void reserve_member_name(const StringName &p_name);
	void reserve_global_class_name(const StringName &p_name);
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
	StringName get_or_create_builtin_class_alias(const StringName &p_name);
	const StringName *get_builtin_class_alias(const StringName &p_name) const;
	const HashMap<StringName, StringName> &get_builtin_class_aliases() const;
};

} // namespace WGodotGDScriptExportTransform
