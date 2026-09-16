// wgodot-changes::file
#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"

// Editor-only semantic input for native export. Parser references own all AST nodes.
class WGodotCppProject {
public:
	struct Class {
		String script_path;
		String cpp_name;
		GDScriptParser::ClassNode *node = nullptr;
	};

private:
	Vector<Ref<GDScriptParserRef>> parsers;
	Vector<Class> classes;
	HashMap<const GDScriptParser::ClassNode *, int> class_indices;
	Vector<String> resource_dependencies;
	Vector<String> diagnostics;

	Error collect_scripts(const String &p_directory, Vector<String> &r_scripts);
	void collect_classes(const String &p_script_path, GDScriptParser::ClassNode *p_class);

public:
	Error analyze();
	const Vector<Class> &get_classes() const { return classes; }
	const Class *find_class(const GDScriptParser::ClassNode *p_node) const;
	const Vector<String> &get_diagnostics() const { return diagnostics; }
	Dictionary describe() const;
};
