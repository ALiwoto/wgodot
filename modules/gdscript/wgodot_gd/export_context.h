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
	HashSet<StringName> reserved_member_names;
	HashSet<StringName> reserved_global_class_names;
	RandomPCG obfuscation_random;

	void reserve_registered_global_class_names();

public:
	void reset();
	void set_options(const TransformOptions &p_options);
	void reserve_member_name(const StringName &p_name);
	void reserve_global_class_name(const StringName &p_name);
	void reserve_script_global_class_name(const GDScriptParser::ClassNode *p_class);
	void seed_reserved_obfuscated_names(HashSet<StringName> &r_reserved_names) const;
	String make_obfuscated_name(HashSet<StringName> &r_reserved_names, const String &p_warning_context);

	static String make_member_key(const String &p_class_key, const StringName &p_member_name);
	static void make_member_keys(const GDScriptParser::ClassNode *p_class, const String &p_script_path, const StringName &p_member_name, Vector<String> &r_keys);

	void index_script(const GDScriptParser::ClassNode *p_class, const String &p_script_path);
	String get_or_create_member_rename(const String &p_key);
	void bind_member_rename(const String &p_key, const String &p_obfuscated_name);
	const String *get_member_rename(const String &p_key) const;
};

} // namespace WGodotGDScriptExportTransform
