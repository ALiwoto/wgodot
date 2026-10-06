// wgodot-changes::file
#pragma once

#include "core/templates/vector.h"

class GDScript;

// Loading an uncached base script can start another top-level export update.
// Keep its inheritance walk separate and restore the enclosing walk afterwards.
class WGodotExportInheritanceScope {
	Vector<GDScript *> &stack;
	Vector<GDScript *> enclosing;
	bool recursive;

public:
	WGodotExportInheritanceScope(Vector<GDScript *> &p_stack, bool p_recursive) : stack(p_stack), recursive(p_recursive) {
		if (!recursive) {
			SWAP(stack, enclosing);
		}
	}

	~WGodotExportInheritanceScope() {
		if (recursive) {
			stack.resize(stack.size() - 1);
		} else {
			SWAP(stack, enclosing);
		}
	}
};
