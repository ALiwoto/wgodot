// wgodot-changes::file

#pragma once

#include "core/error/error_list.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

namespace WGodotGDScriptExportTransform {

struct SourceEdit {
	int start = 0;
	int end = 0;
	String text;
};

struct SourceOrigin {
	int original_offset = -1;
	StringName generated_by;
};

class ExportSource {
	struct OriginSpan {
		int start = 0;
		int end = 0;
		SourceOrigin origin;
		bool linear = true;
	};

	String original;
	String text;
	Vector<OriginSpan> origins;

	void append_original_range(int p_start, int p_end, ExportSource &r_output) const;
	Error apply(const Vector<SourceEdit> &p_edits, const StringName &p_pass, ExportSource &r_output, String &r_error) const;
	friend class ExportProject;

public:
	const String &get_text() const { return text; }
	const String &get_original() const { return original; }
	SourceOrigin get_origin(int p_offset) const;
	int get_original_line(int p_offset) const;
};

// Source buffers are shared between revisions until an edit replaces them.
// A revision is only modified while capturing it or constructing its successor.
class ExportProject {
	uint64_t revision = 0;
	HashMap<String, ExportSource> sources;
	Vector<String> script_paths;
	HashSet<String> exported_paths;
	HashMap<StringName, String> external_classes;
	HashMap<String, StringName> external_native_bases;

public:
	Error capture(const HashSet<String> &p_exported_paths, const HashSet<String> &p_script_paths, String &r_error);
	Error apply(const HashMap<String, Vector<SourceEdit>> &p_edits, const StringName &p_pass, ExportProject &r_output, String &r_error) const;
	uint64_t get_revision() const { return revision; }
	const ExportSource *get_source(const String &p_path) const { return sources.getptr(p_path); }
	const Vector<String> &get_script_paths() const { return script_paths; }
	const HashSet<String> &get_exported_paths() const { return exported_paths; }
	const HashMap<StringName, String> &get_external_classes() const { return external_classes; }
	const HashMap<String, StringName> &get_external_native_bases() const { return external_native_bases; }
	bool is_exported(const String &p_path) const { return exported_paths.has(p_path); }
};

} // namespace WGodotGDScriptExportTransform
