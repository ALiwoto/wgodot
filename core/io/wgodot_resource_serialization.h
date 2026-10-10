// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"

#ifdef TOOLS_ENABLED
// A scoped export-only transform. Godot's serializer still owns the resource
// format, offsets and compression; loaded resources are never modified.
class WGodotResourceSerialization {
public:
	using Rewrite = String (*)(void *, const String &);

private:
	static thread_local WGodotResourceSerialization *current;
	WGodotResourceSerialization *previous;
	Rewrite callback;
	void *context;

public:
	WGodotResourceSerialization(Rewrite p_callback, void *p_context);
	~WGodotResourceSerialization();
	static String rewrite(const String &p_value);
	static bool is_active() { return current != nullptr; }
};
#endif
