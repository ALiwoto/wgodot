// wgodot-changes::file
#include "wgodot_cpp_project.h"

#include "wgodot_cpp_ast.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"


namespace {
class ResourceDependencies : public WGodotCppAstVisitor {
	bool visit(const GDScriptParser::Node *p_node) override {
		if (p_node->type == GDScriptParser::Node::PRELOAD) {
			const auto *preload = static_cast<const GDScriptParser::PreloadNode *>(p_node);
			if (preload->resource.is_valid() && !Object::cast_to<Script>(preload->resource.ptr())) {
				const String path = preload->resource->get_path();
				if (auto *existing = paths.getptr(path)) {
					existing->asynchronous &= preload->wgodot_async;
				} else {
					paths.insert(path, { path, preload->resource->get_class(), preload->wgodot_async });
				}
			}
		}
		return true;
	}

public:
	HashMap<String, WGodotCppProject::Preload> paths;
};
} // namespace

Error WGodotCppProject::collect_scripts(const String &p_directory, Vector<String> &r_scripts) {
	Error error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &error);
	if (error != OK) {
		diagnostics.push_back(vformat("Cannot read %s: %s", p_directory, error_names[error]));
		return error;
	}
	if (directory->file_exists(".gdignore")) {
		return OK;
	}
	directory->list_dir_begin();
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry.begins_with(".")) {
			continue;
		}
		const String path = p_directory.path_join(entry);
		if (directory->current_is_dir()) {
			// Imported resources and the global class cache obey this same boundary.
			if (directory->is_link(entry)) {
				diagnostics.push_back("Native export does not support directory symlinks: " + path);
				return ERR_UNAVAILABLE;
			}
			error = collect_scripts(path, r_scripts);
			if (error != OK) {
				return error;
			}
		} else if (entry.get_extension() == "gd") {
			r_scripts.push_back(path);
		}
	}
	return OK;
}

void WGodotCppProject::collect_classes(const String &p_script_path, GDScriptParser::ClassNode *p_class) {
	Class entry;
	entry.script_path = p_script_path;
	entry.cpp_name = "Game_" + (p_script_path + "::" + p_class->fqcn).sha256_text().substr(0, 16);
	entry.node = p_class;
	class_indices.insert(p_class, classes.size());
	classes.push_back(entry);
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			collect_classes(p_script_path, member.m_class);
		}
	}
}

Error WGodotCppProject::analyze() {
	ERR_FAIL_COND_V(!ProjectSettings::get_singleton()->is_project_loaded(), ERR_UNCONFIGURED);
	classes.clear();
	class_indices.clear();
	resource_dependencies.clear();
	preloads.clear();
	diagnostics.clear();
	parsers.clear();
	if (!GLOBAL_GET("wgodot/gdscript/strict_type_checking")) {
		diagnostics.push_back("Native export requires strict type checking. Enable 'wgodot/gdscript/strict_type_checking' in Project Settings.");
		return ERR_UNCONFIGURED;
	}
	if (!GLOBAL_GET("wgodot/gdscript/strict_signal_callable_checking")) {
		diagnostics.push_back("Native export requires strict signal/callable checking. Enable 'wgodot/gdscript/strict_signal_callable_checking' in Project Settings.");
		return ERR_UNCONFIGURED;
	}
	Vector<String> scripts;
	Error error = collect_scripts("res://", scripts);
	if (error != OK) {
		return error;
	}
	scripts.sort();
	ResourceDependencies dependencies;
	for (const String &path : scripts) {
		Ref<GDScriptParserRef> parser = GDScriptCache::get_parser(path, GDScriptParserRef::FULLY_SOLVED, error);
		if (parser.is_valid()) {
			parsers.push_back(parser);
		}
		if (error != OK) {
			if (parser.is_valid()) {
				for (const GDScriptParser::ParserError &parse_error : parser->get_parser()->get_errors()) {
					diagnostics.push_back(vformat("%s:%d:%d: %s", path, parse_error.start_line, parse_error.start_column, parse_error.message));
				}
			}
			diagnostics.push_back(vformat("Cannot analyze %s: %s", path, error_names[error]));
			continue;
		}
		collect_classes(path, parser->get_parser()->get_tree());
		if (!dependencies.walk(parser->get_parser()->get_tree())) {
			diagnostics.push_back("Native export encountered an unsupported syntax tree node in " + path);
		}
	}
	for (const auto &dependency : dependencies.paths) {
		resource_dependencies.push_back(dependency.key);
	}
	resource_dependencies.sort();
	for (const String &path : resource_dependencies) {
		preloads.push_back(dependencies.paths[path]);
	}
	return diagnostics.is_empty() ? OK : ERR_PARSE_ERROR;
}

const WGodotCppProject::Class *WGodotCppProject::find_class(const GDScriptParser::ClassNode *p_node) const {
	const int *index = class_indices.getptr(p_node);
	return index ? &classes[*index] : nullptr;
}

GDScriptParser *WGodotCppProject::find_parser(const String &p_script_path) const {
	for (const auto &parser : parsers) {
		if (parser->get_path() == p_script_path) {
			return parser->get_parser();
		}
	}
	return nullptr;
}

GDScriptAnalyzer *WGodotCppProject::find_analyzer(const String &p_script_path) const {
	for (const auto &parser : parsers) {
		if (parser->get_path() == p_script_path) {
			return parser->get_analyzer();
		}
	}
	return nullptr;
}

Dictionary WGodotCppProject::describe() const {
	Dictionary result;
	result["format"] = 1;
	result["script_count"] = parsers.size();
	result["resource_dependencies"] = resource_dependencies;
	Array preload_list;
	for (const Preload &preload : preloads) {
		Dictionary entry;
		entry["path"] = preload.path;
		entry["type"] = preload.type;
		entry["async"] = preload.asynchronous;
		preload_list.push_back(entry);
	}
	result["preloads"] = preload_list;
	result["diagnostics"] = diagnostics;
	Array class_list;
	for (const Class &entry : classes) {
		Dictionary description;
		description["source"] = entry.script_path;
		description["name"] = entry.node->fqcn;
		description["cpp_name"] = entry.cpp_name;
		description["base"] = entry.node->base_type.to_string();
		description["interface"] = entry.node->wgodot_is_interface;
		Array members;
		for (const GDScriptParser::ClassNode::Member &member : entry.node->members) {
			Dictionary item;
			item["name"] = member.get_name();
			item["kind"] = int(member.type);
			if (member.type == GDScriptParser::ClassNode::Member::FUNCTION) {
				item["return_type"] = member.function->return_type_constraint.to_string();
				item["async"] = member.function->is_coroutine;
				item["static"] = member.function->is_static;
				Array parameters;
				for (const GDScriptParser::ParameterNode *parameter : member.function->parameters) {
					Dictionary argument;
					argument["name"] = parameter->identifier->name;
					argument["type"] = parameter->type_constraint.to_string();
					parameters.push_back(argument);
				}
				item["parameters"] = parameters;
			} else if (member.type == GDScriptParser::ClassNode::Member::VARIABLE) {
				item["type"] = member.variable->type_constraint.to_string();
				item["static"] = member.variable->is_static;
				item["onready"] = member.variable->onready;
				item["exported"] = member.variable->exported;
			}
			members.push_back(item);
		}
		description["members"] = members;
		class_list.push_back(description);
	}
	result["classes"] = class_list;
	return result;
}
