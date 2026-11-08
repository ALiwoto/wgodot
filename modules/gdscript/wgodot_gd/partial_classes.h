// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace WGodotPartialClasses {
struct SourcePart {
	String path;
	int first_line = 1;
	int line_count = 0;
};

struct SourceLocation {
	String path;
	int line = 1;
};

SourceLocation locate(const Vector<SourcePart> &p_parts, const String &p_path, int p_line);

String canonical_path(const String &p_path);
Vector<String> paths(const String &p_path);
Error compose(const String &p_source, const String &p_path, String &r_source, String &r_path, Vector<SourcePart> &r_parts, String &r_error);
void clear();
} // namespace WGodotPartialClasses
