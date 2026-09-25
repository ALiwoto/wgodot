// wgodot-changes::file
#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/wgodot_gd/editor/export/export_analysis.h"
#include "modules/gdscript/wgodot_gd/editor/export/export_context.h"

// Editor-only semantic input for native export. Parser references own all AST nodes.
class WGodotCppProject {
	friend class WGodotCppEmitter;
	friend class WGodotCppSignatures;

public:
	struct Preload {
		String path;
		String type;
		bool asynchronous = false;
	};
	struct Class {
		String script_path;
		String cpp_name;
		GDScriptParser::ClassNode *node = nullptr;
	};

private:
	WGodotGDScriptExportTransform::ExportProject export_project;
	WGodotGDScriptExportTransform::ExportContext export_context;
	WGodotGDScriptExportTransform::ExportAnalysis *export_analysis = nullptr;
	Vector<Ref<GDScriptParserRef>> parsers;
	Vector<Class> classes;
	HashMap<const GDScriptParser::ClassNode *, int> class_indices;
	Vector<String> resource_dependencies;
	Vector<Preload> preloads;
	Vector<String> diagnostics;

	Error collect_scripts(const String &p_directory, Vector<String> &r_scripts);
	Error prepare_sources(const Vector<String> &p_scripts);
	void collect_classes(const String &p_script_path, GDScriptParser::ClassNode *p_class);

public:
	WGodotCppProject() = default;
	~WGodotCppProject();
	WGodotCppProject(const WGodotCppProject &) = delete;
	WGodotCppProject &operator=(const WGodotCppProject &) = delete;

	Error analyze();
	const Vector<Class> &get_classes() const { return classes; }
	const Vector<Preload> &get_preloads() const { return preloads; }
	const Class *find_class(const GDScriptParser::ClassNode *p_node) const;
	GDScriptParser *find_parser(const String &p_script_path) const;
	GDScriptAnalyzer *find_analyzer(const String &p_script_path) const;
	const Vector<String> &get_diagnostics() const { return diagnostics; }
	Dictionary describe() const;
};
