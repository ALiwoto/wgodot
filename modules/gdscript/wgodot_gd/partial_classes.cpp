// wgodot-changes::file
#include "partial_classes.h"
#include "script_resolution.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/profiling/wgodot_startup_profile.h"

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
	String source_stamp;
};

Mutex mutex;
HashMap<String, Header> headers;
HashMap<String, Vector<String>> groups;
HashSet<String> indexed_directories;
HashMap<String, String> class_directories;
HashMap<String, String> conflicting_directories;

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

struct IndexCache {
	Dictionary previous_directories;
	Dictionary previous_scripts;
	Dictionary directories;
	Dictionary scripts;
	String path;
	bool changed = false;
	bool stable = true;

	IndexCache(const String &p_directory) {
		const String &build = WGodotBuildFingerprint::get();
		if (build.is_empty()) {
			return;
		}
		path = ProjectSettings::get_singleton()->get_project_data_path().path_join("partial_class_cache").path_join(build).path_join(p_directory.trim_prefix("res://")).path_join("index.bin");
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			return;
		}
		const Variant stored = file->get_var(false);
		if (stored.get_type() != Variant::DICTIONARY) {
			return;
		}
		const Dictionary data = stored;
		if (int(data.get("version", 0)) == 2) {
			previous_directories = data.get("directories", Dictionary());
			previous_scripts = data.get("scripts", Dictionary());
		}
	}

	Array list(const String &p_path) {
		const String stamp = WGodotFileStamp::get(p_path);
		const Array previous = previous_directories.get(p_path, Array());
		if (previous.size() == 2 && previous[0] == Variant(stamp) && stamp != "<missing>") {
			directories[p_path] = previous;
			return previous;
		}
		changed = true;
		Array sources;
		Ref<DirAccess> directory = DirAccess::open(p_path);
		if (directory.is_valid() && !directory->file_exists(".gdignore")) {
			directory->list_dir_begin();
			for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
				if (name.begins_with(".") || directory->is_link(name)) {
					continue;
				}
				const String child = p_path.path_join(name);
				if (!directory->current_is_dir() && name.get_extension() == "gd") {
					sources.push_back(child);
				}
			}
		}
		stable &= stamp == WGodotFileStamp::get(p_path);
		const Array result{ stamp, sources };
		directories[p_path] = result;
		return result;
	}

	Header read(const String &p_path) {
		const String stamp = WGodotFileStamp::get(p_path);
		const Array previous = previous_scripts.get(p_path, Array());
		Header result;
		if (previous.size() == 6 && previous[0] == Variant(stamp) && stamp != "<missing>") {
			result.source_stamp = stamp;
			result.name = previous[1];
			result.base = previous[2];
			result.partial = previous[3];
			for (const Variant &line : Array(previous[4])) {
				result.lines.push_back(line);
			}
			for (const Array annotation : Array(previous[5])) {
				result.annotations.push_back({ StringName(annotation[0]), String(annotation[1]) });
			}
			scripts[p_path] = previous;
			return result;
		}
		changed = true;
		const String source = FileAccess::get_file_as_string(p_path);
		if (source.contains("@partial")) {
			result = read_header(source, p_path);
		}
		if (stamp == WGodotFileStamp::get(p_path) && stamp != "<missing>") {
			result.source_stamp = stamp;
		} else {
			stable = false;
		}
		Array lines;
		for (int line : result.lines) {
			lines.push_back(line);
		}
		Array annotations;
		for (const auto &annotation : result.annotations) {
			annotations.push_back(Array{ annotation.name, annotation.source });
		}
		scripts[p_path] = Array{ stamp, result.name, result.base, result.partial, lines, annotations };
		return result;
	}

	void save() {
		if (path.is_empty() || !stable || (!changed && directories.size() == previous_directories.size() && scripts.size() == previous_scripts.size())) {
			return;
		}
		if (DirAccess::make_dir_recursive_absolute(path.get_base_dir()) != OK) {
			return;
		}
		const String temporary = path + "." + itos(OS::get_singleton()->get_process_id()) + "." + uitos(Thread::get_caller_id()) + ".tmp";
		Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_var(Dictionary{ { "version", 2 }, { "directories", directories }, { "scripts", scripts } }, false);
			const Error error = file->get_error();
			file.unref();
			if (error == OK) {
				DirAccess::rename_absolute(temporary, path);
			}
		}
	}
};

void ensure_directory(const String &p_directory) {
	if (indexed_directories.has(p_directory) || !p_directory.begins_with("res://") || !ProjectSettings::get_singleton()->is_project_loaded()) {
		return;
	}
	indexed_directories.insert(p_directory);
	WGodotStartupProfile::Scope profile("GDScript partial class directory", p_directory);
	IndexCache cache(p_directory);
	const Array listing = cache.list(p_directory);
	HashSet<String> names;
	for (const String path : Array(listing[1])) {
		const Header header = cache.read(path);
		if (!header.partial || header.name.is_empty()) {
			continue;
		}
		groups[p_directory.path_join(header.name)].push_back(path);
		headers.insert(path, header);
		names.insert(header.name);
		if (const String *previous = class_directories.getptr(header.name)) {
			if (*previous != p_directory) {
				conflicting_directories.insert(header.name, p_directory);
			}
		} else {
			class_directories.insert(header.name, p_directory);
		}
	}
	cache.save();
	for (const String &name : names) {
		groups[p_directory.path_join(name)].sort();
	}
}

Vector<String> group_paths(const String &p_path) {
	ensure_directory(p_path.get_base_dir());
	const Header *header = headers.getptr(p_path);
	if (!header || !header->partial) {
		return {};
	}
	return groups[p_path.get_base_dir().path_join(header->name)];
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

String source_stamp(const String &p_path) {
	MutexLock lock(mutex);
	ensure_directory(p_path.get_base_dir());
	const Header *header = headers.getptr(p_path);
	return header ? header->source_stamp : String();
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
	String other_directory;
	const String directory = p_path.get_base_dir();
	if (WGodotGDScriptResolution::is_global_class(own.name)) {
		const String registered = WGodotGDScriptResolution::get_global_class_path(own.name).get_base_dir();
		if (registered != directory) {
			other_directory = registered;
		}
	}
	{
		MutexLock lock(mutex);
		if (conflicting_directories.has(own.name)) {
			const String &first = class_directories[own.name];
			other_directory = first == directory ? conflicting_directories[own.name] : first;
		}
	}
	if (!other_directory.is_empty()) {
		r_error = vformat("All declarations of partial class \"%s\" must be in the same directory. Found \"%s\" and \"%s\".", own.name, directory, other_directory);
		return ERR_PARSE_ERROR;
	}
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
	indexed_directories.clear();
	class_directories.clear();
	conflicting_directories.clear();
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
