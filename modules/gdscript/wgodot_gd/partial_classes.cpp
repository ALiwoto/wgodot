// wgodot-changes::file
#include "partial_classes.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"

#include "modules/gdscript/gdscript_parser.h"
#ifdef TOOLS_ENABLED
#include "modules/gdscript/wgodot_gd/editor/export/export_project.h"
#endif

namespace WGodotPartialClasses {
namespace {
struct Header {
	struct Annotation {
		StringName name;
		String source;
	};
	String name;
	String base;
	Vector<int> lines;
	Vector<Annotation> annotations;
	bool partial = false;
};

Mutex mutex;
HashMap<String, Header> headers;
HashMap<String, Vector<String>> groups;
bool indexed = false;

Header read_header(const String &p_source, const String &p_path) {
	GDScriptParser parser;
	parser.parse(p_source, p_path, false, false);
	const auto *node = parser.get_tree();
	Header header;
	if (!node || !node->identifier) {
		return header;
	}
	header.name = node->identifier->name;
	header.lines.push_back(node->identifier->start_line);
	const Vector<String> source_lines = p_source.split("\n");
	for (const auto *annotation : node->annotations) {
		if (annotation->name == "@partial") {
			header.partial = true;
		} else {
			Header::Annotation entry;
			entry.name = annotation->name;
			for (int line = annotation->start_line; line <= annotation->end_line; ++line) {
				entry.source += source_lines[line - 1].trim_suffix("\r") + "\n";
			}
			if (annotation->name == "@icon") {
				entry.source = "@icon(\"" + node->simplified_icon_path.c_escape() + "\")\n";
			}
			header.annotations.push_back(entry);
		}
		for (int line = annotation->start_line; line <= annotation->end_line; ++line) {
			header.lines.push_back(line);
		}
	}
	if (node->extends_used) {
		if (!node->extends_path.is_empty()) {
			String path = node->extends_path;
			if (path.is_relative_path()) {
				path = p_path.get_base_dir().path_join(path).simplify_path();
			}
			header.base = "\"" + path.c_escape() + "\"";
		}
		for (const auto *identifier : node->extends) {
			if (!header.base.is_empty()) {
				header.base += ".";
			}
			header.base += identifier->name;
		}
		for (int line = node->extends_start_line; line <= node->extends_end_line; ++line) {
			header.lines.push_back(line);
		}
	}
	return header;
}

void scan(const String &p_path) {
	Ref<DirAccess> directory = DirAccess::open(p_path);
	if (directory.is_null() || directory->file_exists(".gdignore")) {
		return;
	}
	directory->list_dir_begin();
	for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
		if (name.begins_with(".") || directory->is_link(name)) {
			continue;
		}
		const String path = p_path.path_join(name);
		if (directory->current_is_dir()) {
			scan(path);
		} else if (name.get_extension() == "gd") {
			Header header = read_header(FileAccess::get_file_as_string(path), path);
			if (!header.name.is_empty()) {
				groups[header.name].push_back(path);
				headers.insert(path, header);
			}
		}
	}
}

void ensure_index() {
	if (indexed || !ProjectSettings::get_singleton()->is_project_loaded()) {
		return;
	}
	indexed = true;
	scan("res://");
	for (auto &entry : groups) {
		entry.value.sort();
	}
}

Vector<String> group_paths(const String &p_path) {
	ensure_index();
	const Header *header = headers.getptr(p_path);
	if (!header || !header->partial) {
		return {};
	}
	const Vector<String> &paths = groups[header->name];
	for (const String &path : paths) {
		if (!headers[path].partial) {
			return {}; // The ordinary duplicate-class diagnostic applies.
		}
	}
	return paths;
}
} // namespace

String canonical_path(const String &p_path) {
	if (!p_path.begins_with("res://") || p_path.get_extension() != "gd") {
		return p_path;
	}
	MutexLock lock(mutex);
	const Vector<String> parts = group_paths(p_path);
	return parts.is_empty() ? p_path : parts[0];
}

Vector<String> paths(const String &p_path) {
	MutexLock lock(mutex);
	return group_paths(p_path);
}

Error compose(const String &p_source, const String &p_path, String &r_source, String &r_path, Vector<SourcePart> &r_parts, String &r_error) {
	r_source = p_source;
	r_path = p_path;
	if (!p_source.contains("@partial")) {
		return OK;
	}
	const Header own = read_header(p_source, p_path);
	if (!own.partial) {
		return OK;
	}
	Vector<String> parts = paths(p_path);
	if (parts.is_empty()) {
		return OK;
	}
	r_path = parts[0];
	String base;
	String annotations;
	HashMap<StringName, String> annotation_sources;
	Vector<String> bodies;
	for (const String &path : parts) {
		const String source = path == p_path ? p_source : FileAccess::get_file_as_string(path);
		const Header header = read_header(source, path);
		if (!header.partial || header.name != own.name) {
			r_error = "Partial class declarations changed; refresh the script filesystem: " + path;
			return ERR_PARSE_ERROR;
		}
		if (!header.base.is_empty()) {
			if (!base.is_empty() && base != header.base) {
				r_error = "Partial class declarations have different base classes: " + path;
				return ERR_PARSE_ERROR;
			}
			base = header.base;
		}
		for (const auto &annotation : header.annotations) {
			if (const String *previous = annotation_sources.getptr(annotation.name)) {
				if (*previous != annotation.source) {
					r_error = "Partial class declarations have conflicting annotations: " + String(annotation.name) + " in " + path;
					return ERR_PARSE_ERROR;
				}
			} else {
				annotation_sources.insert(annotation.name, annotation.source);
				annotations += annotation.source;
			}
		}
		Vector<String> lines = source.split("\n");
		for (int line : header.lines) {
			if (line > 0 && line <= lines.size()) {
				lines.write[line - 1] = "";
			}
		}
		bodies.push_back(String("\n").join(lines));
	}
	r_source = annotations + "@partial\nclass_name " + own.name + "\n";
	if (!base.is_empty()) {
		r_source += "extends " + base + "\n";
	}
	int line = r_source.count("\n") + 1;
	for (int i = 0; i < parts.size(); ++i) {
		SourcePart part;
		part.path = parts[i];
		part.first_line = line;
		part.line_count = bodies[i].count("\n") + 1;
		r_parts.push_back(part);
		r_source += bodies[i] + "\n";
		line += part.line_count;
	}
	return OK;
}

void clear() {
	MutexLock lock(mutex);
	headers.clear();
	groups.clear();
	indexed = false;
}

SourceLocation locate(const Vector<SourcePart> &p_parts, const String &p_path, int p_line) {
	for (const SourcePart &part : p_parts) {
		if (p_line >= part.first_line && p_line < part.first_line + part.line_count) {
			return { part.path, p_line - part.first_line + 1 };
		}
	}
	return { p_path, p_line };
}
} // namespace WGodotPartialClasses

WGodotPartialClasses::SourceLocation GDScriptParser::wgodot_source_location(int p_line) const {
#ifdef TOOLS_ENABLED
	if (wgodot_export_source) {
		return wgodot_export_source->get_location(script_path, p_line);
	}
#endif
	return WGodotPartialClasses::locate(wgodot_partial_sources, script_path, p_line);
}
