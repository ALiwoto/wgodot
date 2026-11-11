// wgodot-changes::file
#pragma once

#include "core/io/wgodot_resource_value.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"

#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/wgodot_gd/editor/export/export_project.h"

// Immutable declaration snapshot shared by resource-check workers. It contains
// no live scripts, parser pointers, resources, or mutable editor caches.
class WGodotResourceTypes {
public:
	using Type = WGodotResourceDataType;
	struct Field {
		Type type;
		bool exposed = false;
	};
	struct Function {
		Vector<Type> parameters;
		int defaults = 0;
		bool exposed = true;
		bool variadic = false;
	};
	struct Class {
		StringName base;
		HashMap<StringName, Field> fields;
		HashMap<StringName, Function> methods;
		HashMap<StringName, Function> signals;
		String signature;
		bool abstract = false;
	};

private:
	HashMap<StringName, Class> classes;
	static Type script_type(const GDScriptParser::DataType &p_type);
	static Type native_type(const PropertyInfo &p_info);

public:
	Error build(const WGodotGDScriptExportTransform::ExportProject &p_project, String &r_error);
	void add_class(const GDScriptParser::ClassNode *p_class);
	bool exists(const StringName &p_class) const;
	bool derives(const StringName &p_class, const StringName &p_base) const;
	bool instantiable(const StringName &p_class) const;
	bool has_resource_exports(const StringName &p_class) const;
	Error check_exports(const StringName &p_class, String &r_error) const;
	bool field(const StringName &p_class, const StringName &p_name, Field &r_field, HashMap<String, String> &r_declarations) const;
	bool function(const StringName &p_class, const StringName &p_name, bool p_signal, Function &r_function, HashMap<String, String> &r_declarations) const;
	void record(const Type &p_type, HashMap<String, String> &r_declarations) const;
	String signature(const StringName &p_class) const;
	bool accepts(const Type &p_expected, const Type &p_actual) const;
};
