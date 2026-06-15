// wgodot-changes::file
/**************************************************************************/
/*  export_context.h                                                      */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "../gdscript_parser.h"

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
	int obfuscated_member_counter = 0;

public:
	void reset();
	void set_options(const TransformOptions &p_options);
	void reserve_member_name(const StringName &p_name);

	static String make_member_key(const String &p_class_key, const StringName &p_member_name);
	static void make_member_keys(const GDScriptParser::ClassNode *p_class, const String &p_script_path, const StringName &p_member_name, Vector<String> &r_keys);

	void index_script(const GDScriptParser::ClassNode *p_class, const String &p_script_path);
	String get_or_create_member_rename(const String &p_key);
	void bind_member_rename(const String &p_key, const String &p_obfuscated_name);
	const String *get_member_rename(const String &p_key) const;
};

} // namespace WGodotGDScriptExportTransform
