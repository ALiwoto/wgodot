// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// A lowered C++ value, before choosing statement or expression syntax.
// Setup owns its temporaries until the enclosing source expression finishes.
struct WGodotCppExpression {
	String code;
	String cpp_type;
	String storage_type;
	Vector<String> setup;
	bool effects = true;
	bool borrowed = false;
	bool invariant = false;
	bool object_pointer = false;
	bool nonnull = false;
	bool read_only = false; // Container storage is known to be frozen.

	WGodotCppExpression() = default;
	WGodotCppExpression(const String &p_code, const String &p_type = String()) : code(p_code), cpp_type(p_type) {}

	String expression() const;
	String statement(int p_indent, bool p_return = false) const;
	String block(const String &p_body, int p_indent) const;
};
