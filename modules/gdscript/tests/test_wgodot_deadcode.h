// wgodot-changes::file
/**************************************************************************/
/*  test_wgodot_deadcode.h                                                */
/**************************************************************************/

#pragma once

#include "../gdscript_analyzer.h"
#include "../gdscript_parser.h"
#include "../wgodot_gd/deadcode.gen.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

static String wgodot_get_deadcode_errors(const GDScriptParser &p_parser) {
	String errors;
	for (const GDScriptParser::ParserError &error : p_parser.get_errors()) {
		if (!errors.is_empty()) {
			errors += "\n";
		}
		errors += vformat("line %d, column %d: %s", error.start_line, error.start_column, error.message);
	}
	return errors;
}

static bool wgodot_validate_deadcode_source(const String &p_source, const String &p_path, String &r_errors) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		r_errors = wgodot_get_deadcode_errors(parser);
		return false;
	}

	GDScriptAnalyzer analyzer(&parser);
	if (analyzer.analyze() != OK) {
		r_errors = wgodot_get_deadcode_errors(parser);
		return false;
	}

	return true;
}

static String wgodot_make_deadcode_source(const char *p_template_source, bool p_static_class) {
	String source;
	if (p_static_class) {
		source += "@static_class\n";
		source += "class_name WGodotDeadCodeStaticTest\n\n";
	} else {
		source += "extends Node\n\n";
	}

	source += String(p_template_source).replace("WGODOT_DC_ID", "123456789");
	source += "\n";
	return source;
}

static void wgodot_validate_deadcode_template(const WGodotGDScriptDeadCodeTemplates::DeadCodeTemplate &p_template, bool p_static_class) {
	REQUIRE_MESSAGE(p_template.name != nullptr, "Deadcode template name must not be null.");
	REQUIRE_MESSAGE(p_template.source != nullptr, "Deadcode template source must not be null.");

	const String template_name = String(p_template.name);
	const String path = p_static_class ? "res://wgodot_deadcode_static/" + template_name + ".gd" : "res://wgodot_deadcode/" + template_name + ".gd";
	const String source = wgodot_make_deadcode_source(p_template.source, p_static_class);

	String errors;
	const bool valid = wgodot_validate_deadcode_source(source, path, errors);
	CHECK_MESSAGE(valid, vformat("Deadcode template '%s' failed parse/analyze:\n%s\n\nSource:\n%s", template_name, errors, source));
}

TEST_CASE("[Modules][GDScript][WGodot] Deadcode snippets parse and analyze") {
	for (int i = 0; i < WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATE_COUNT; i++) {
		wgodot_validate_deadcode_template(WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATES[i], false);
	}

	for (int i = 0; i < WGodotGDScriptDeadCodeTemplates::STATIC_IN_CLASS_DEAD_CODE_TEMPLATE_COUNT; i++) {
		wgodot_validate_deadcode_template(WGodotGDScriptDeadCodeTemplates::STATIC_IN_CLASS_DEAD_CODE_TEMPLATES[i], true);
	}
}

} // namespace GDScriptTests
