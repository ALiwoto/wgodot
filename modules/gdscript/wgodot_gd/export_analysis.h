// wgodot-changes::file

#pragma once

#include "../gdscript_cache.h"
#include "export_project.h"

namespace WGodotGDScriptExportTransform {

class ExportContext;

// Owns all language-front-end state for one fixed project revision.
class ExportAnalysis {
	const ExportProject &project;
	const ExportContext &artifacts;
	HashMap<String, Ref<GDScriptParserRef>> parsers;
	HashMap<String, Ref<GDScript>> scripts;
	HashMap<StringName, String> global_classes;
	HashMap<String, StringName> external_native_bases;
	HashMap<String, String> path_aliases;
	HashMap<StringName, StringName> native_aliases;
	HashMap<StringName, StringName> function_aliases;
	HashMap<StringName, StringName> member_aliases[4];
	static thread_local ExportAnalysis *active;

public:
	// Bridges Godot's static cache APIs without replacing or populating the
	// editor cache. The scope never outlives the analysis operation using it.
	class Scope {
		ExportAnalysis *previous = nullptr;

	public:
		explicit Scope(ExportAnalysis &p_analysis);
		~Scope();
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
	};

	ExportAnalysis(const ExportProject &p_project, const ExportContext &p_artifacts);
	~ExportAnalysis();
	ExportAnalysis(const ExportAnalysis &) = delete;
	ExportAnalysis &operator=(const ExportAnalysis &) = delete;
	static ExportAnalysis *get_active() { return active; }
	const ExportContext &get_artifacts() const { return artifacts; }
	String resolve_path(const String &p_path) const;
	const ExportSource *get_source(const String &p_path) const;
	Ref<GDScriptParserRef> get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error);
	Ref<GDScript> get_shallow_script(const String &p_path, Error &r_error);
	Error analyze_scripts(bool p_bodies, String &r_error);
	StringName resolve_native_alias(const StringName &p_name) const;
	StringName resolve_function_alias(const StringName &p_name) const;
	StringName resolve_member_alias(const StringName &p_name, bool p_static, bool p_property) const;

	static bool is_global_class(const StringName &p_name);
	static String get_global_class_path(const StringName &p_name);
	static StringName get_global_class_native_base(const StringName &p_name);
	static Ref<Script> load_script(const String &p_path, const String &p_type = String());
	static bool resource_exists(const String &p_path);
	static String get_resource_type(const String &p_path);
};

} // namespace WGodotGDScriptExportTransform
