// wgodot-changes::file

#include "export_project.h"

#include "../wgodot_stdlib.h"

#include "core/io/file_access.h"
#include "core/object/script_language.h"

namespace WGodotGDScriptExportTransform {

namespace {
struct EditOrder {
	bool operator()(const SourceEdit &p_left, const SourceEdit &p_right) const {
		return p_left.start < p_right.start || (p_left.start == p_right.start && p_left.end < p_right.end);
	}
};
} // namespace

SourceOrigin ExportSource::get_origin(int p_offset) const {
	for (const OriginSpan &span : origins) {
		if (p_offset >= span.start && p_offset < span.end) {
			SourceOrigin origin = span.origin;
			if (span.linear && origin.original_offset >= 0) {
				origin.original_offset += p_offset - span.start;
			}
			return origin;
		}
	}
	return SourceOrigin();
}

int ExportSource::get_original_line(int p_offset) const {
	const int original_offset = get_origin(p_offset).original_offset;
	if (original_offset < 0) {
		return -1;
	}
	int line = 1;
	for (int i = 0; i < original_offset; i++) {
		if (original[i] == '\n') {
			line++;
		}
	}
	return line;
}

void ExportSource::append_original_range(int p_start, int p_end, ExportSource &r_output) const {
	const int output_start = r_output.text.length();
	r_output.text += text.substr(p_start, p_end - p_start);
	for (const OriginSpan &span : origins) {
		const int start = MAX(p_start, span.start);
		const int end = MIN(p_end, span.end);
		if (start >= end) {
			continue;
		}
		OriginSpan copied = span;
		copied.start = output_start + start - p_start;
		copied.end = output_start + end - p_start;
		if (copied.linear && copied.origin.original_offset >= 0) {
			copied.origin.original_offset += start - span.start;
		}
		r_output.origins.push_back(copied);
	}
}

Error ExportSource::apply(const Vector<SourceEdit> &p_edits, const StringName &p_pass, ExportSource &r_output, String &r_error) const {
	Vector<SourceEdit> edits = p_edits;
	edits.sort_custom<EditOrder>();
	int cursor = 0;
	int previous_start = -1;
	for (const SourceEdit &edit : edits) {
		if (edit.start < cursor || edit.start == previous_start || edit.end < edit.start || edit.end > text.length()) {
			r_error = vformat("Pass '%s' produced overlapping or invalid source edits at [%d, %d).", p_pass, edit.start, edit.end);
			return ERR_INVALID_DATA;
		}
		cursor = edit.end;
		previous_start = edit.start;
	}

	ExportSource output;
	output.original = original;
	cursor = 0;
	for (const SourceEdit &edit : edits) {
		append_original_range(cursor, edit.start, output);
		if (!edit.text.is_empty()) {
			OriginSpan span;
			span.start = output.text.length();
			span.end = span.start + edit.text.length();
			span.linear = false;
			if (edit.start != edit.end) {
				span.origin = get_origin(edit.start);
			} else {
				span.origin.generated_by = p_pass;
			}
			output.origins.push_back(span);
			output.text += edit.text;
		}
		cursor = edit.end;
	}
	append_original_range(cursor, text.length(), output);
	r_output = output;
	return OK;
}

Error ExportProject::capture(const HashSet<String> &p_exported_paths, const HashSet<String> &p_script_paths, String &r_error) {
	ExportProject captured;
	captured.exported_paths = p_exported_paths;
	LocalVector<StringName> registered_classes;
	ScriptServer::get_global_class_list(registered_classes);
	for (const StringName &name : registered_classes) {
		const String path = ScriptServer::get_global_class_path(name);
		if (path.get_extension() != "gd") {
			captured.external_native_bases[path] = ScriptServer::get_global_class_native_base(name);
			captured.external_classes[name] = path;
		}
	}
	for (const String &path : p_script_paths) {
		captured.script_paths.push_back(path);
	}
	captured.script_paths.sort();
	for (const String &path : captured.script_paths) {
		ExportSource source;
		if (WGodotGDScriptStdLib::has_script_path(path)) {
			source.text = WGodotGDScriptStdLib::get_script_source(path);
		} else {
			Error error = OK;
			source.text = FileAccess::get_file_as_string(path, &error);
			if (error != OK) {
				r_error = "Cannot capture GDScript export input: " + path;
				return error;
			}
		}
		source.original = source.text;
		if (!source.text.is_empty()) {
			ExportSource::OriginSpan span;
			span.end = source.text.length();
			span.origin.original_offset = 0;
			source.origins.push_back(span);
		}
		captured.sources.insert(path, source);
	}
	*this = captured;
	return OK;
}

Error ExportProject::apply(const HashMap<String, Vector<SourceEdit>> &p_edits, const StringName &p_pass, ExportProject &r_output, String &r_error) const {
	ExportProject output = *this;
	bool changed = false;
	for (const KeyValue<String, Vector<SourceEdit>> &entry : p_edits) {
		const ExportSource *source = sources.getptr(entry.key);
		if (source == nullptr || !is_exported(entry.key)) {
			r_error = vformat("Pass '%s' tried to edit a script outside the export: %s", p_pass, entry.key);
			return ERR_INVALID_DATA;
		}
		if (entry.value.is_empty()) {
			continue;
		}
		ExportSource transformed;
		Error error = source->apply(entry.value, p_pass, transformed, r_error);
		if (error != OK) {
			r_error = entry.key + ": " + r_error;
			return error;
		}
		if (transformed.get_text() != source->get_text()) {
			output.sources[entry.key] = transformed;
			changed = true;
		}
	}
	output.revision += changed ? 1 : 0;
	r_output = output;
	return OK;
}

} // namespace WGodotGDScriptExportTransform
