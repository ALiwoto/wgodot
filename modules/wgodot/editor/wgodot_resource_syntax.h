// wgodot-changes::file
#pragma once

#include "core/io/file_access.h"
#include "core/templates/vector.h"

namespace WGodotResourceSyntax {
struct Result {
	Error error = OK;
	int line = 1;
	String message;
	Vector<int> missing_newlines;
};

// Parses source without loading dependencies, constructing scenes or running scripts.
Result check(const Ref<FileAccess> &p_file);
} // namespace WGodotResourceSyntax
