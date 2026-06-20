// wgodot-changes::file
/**************************************************************************/
/*  export_source_analysis.cpp                                            */
/**************************************************************************/

#include "export_transform_internal.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

namespace WGodotGDScriptExportTransform {

String get_source_line_text(const String &p_source, int p_line) {
	if (p_line <= 0) {
		return String();
	}

	int current_line = 1;
	int line_start = 0;
	for (int i = 0; i <= p_source.length(); i++) {
		if (i == p_source.length() || p_source[i] == '\n') {
			if (current_line == p_line) {
				int line_end = i;
				if (line_end > line_start && p_source[line_end - 1] == '\r') {
					line_end--;
				}
				return p_source.substr(line_start, line_end - line_start);
			}
			current_line++;
			line_start = i + 1;
		}
	}

	return String();
}

String make_caret_line(int p_column) {
	const int caret_column = MAX(p_column, 1);
	String caret;
	for (int i = 1; i < caret_column; i++) {
		caret += " ";
	}
	caret += "^";
	return caret;
}

String get_parser_errors_with_source_text(const GDScriptParser &p_parser, const String &p_source) {
	const List<GDScriptParser::ParserError> &errors = p_parser.get_errors();
	if (errors.is_empty()) {
		return "no parser/analyzer error details were reported";
	}

	String details;
	const int max_errors = 5;
	int error_count = 0;
	for (const GDScriptParser::ParserError &error : errors) {
		if (error_count >= max_errors) {
			details += "\n  ...";
			break;
		}

		if (!details.is_empty()) {
			details += "\n";
		}
		details += vformat("  line %d, column %d: %s", error.start_line, error.start_column, error.message);
		const String source_line = get_source_line_text(p_source, error.start_line);
		if (!source_line.is_empty()) {
			details += "\n    " + source_line;
			details += "\n    " + make_caret_line(error.start_column);
		}
		error_count++;
	}

	return details;
}

bool parse_only(const String &p_source, const String &p_path, String *r_error_details) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_with_source_text(parser, p_source);
		}
		return false;
	}

	return true;
}

bool AnalyzedSource::load(const String &p_source, const String &p_path, String *r_error_details) {
	if (load_from_cache(p_source, p_path)) {
		return true;
	}

	return load_local(p_source, p_path, r_error_details);
}

bool AnalyzedSource::load_from_cache(const String &p_source, const String &p_path) {
	if (!GDScriptCache::has_parser(p_path)) {
		return false;
	}

	Error err = OK;
	cached_parser_ref = GDScriptCache::get_parser(p_path, GDScriptParserRef::FULLY_SOLVED, err);
	if (err != OK || cached_parser_ref.is_null()) {
		cached_parser_ref.unref();
		return false;
	}

	if (cached_parser_ref->get_source_hash() != p_source.hash()) {
		cached_parser_ref.unref();
		return false;
	}

	parser = cached_parser_ref->get_parser();
	return parser != nullptr;
}

bool AnalyzedSource::load_local(const String &p_source, const String &p_path, String *r_error_details) {
	if (local_parser.parse(p_source, p_path, false) != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_with_source_text(local_parser, p_source);
		}
		return false;
	}

	GDScriptAnalyzer analyzer(&local_parser);
	if (analyzer.analyze() != OK) {
		if (r_error_details != nullptr) {
			*r_error_details = get_parser_errors_with_source_text(local_parser, p_source);
		}
		return false;
	}

	parser = &local_parser;
	return true;
}

} // namespace WGodotGDScriptExportTransform
