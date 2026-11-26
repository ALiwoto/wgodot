// wgodot-changes::file
#pragma once

#include "wgodot_resource_encode.h"

class WGodotResourceSnapshot {
public:
	static constexpr int VERSION = 1;
	static constexpr const char *FILENAME = "resource_schemas.bin";
	static constexpr const char *PACK_IDENTITY = "res://.wgodot/native_resources.bin";
	struct Field {
		StringName name;
		WGodotResourceEncode::Type type;
	};
	struct Schema {
		uint64_t id = 0;
		uint64_t fingerprint = 0;
		String cpp_name;
		String script;
		bool concrete = false;
		PackedStringArray ancestors;
		Vector<Field> fields;
		const Field *field(const StringName &p_name) const;
	};
	HashMap<StringName, Schema> classes;
	Dictionary settings;
	Dictionary scripts;
	Error load(const String &p_path);
	bool derives(const StringName &p_class, const StringName &p_base) const;
	static Dictionary capture_class(const GDScriptParser::ClassNode *p_class, const String &p_script, const String &p_cpp_name);
	static Dictionary settings_revisions();
	static PackedByteArray pack_identity(const Dictionary &p_manifest);
};
