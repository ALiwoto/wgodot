// wgodot-changes::file
/**************************************************************************/
/*  deadcode_injection.h                                                  */
/**************************************************************************/

#pragma once

#include "export_project.h"
#include "export_transform.h"

#include "core/string/ustring.h"

#include "modules/gdscript/gdscript_parser.h"

namespace WGodotGDScriptDeadCodeInjection {

struct Insertion {
	int offset = 0;
	String indent;
	bool static_class = false;
};

void analyze_in_class_dead_code(const String &p_source, const GDScriptParser::ClassNode *p_tree, Vector<Insertion> &r_insertions);
void make_in_class_dead_code_edits(const String &p_source, const String &p_path, const WGodotGDScriptExportTransform::TransformOptions &p_options, const Vector<Insertion> &p_insertions, Vector<WGodotGDScriptExportTransform::SourceEdit> &r_edits);

} // namespace WGodotGDScriptDeadCodeInjection
