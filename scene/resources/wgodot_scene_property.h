// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

class Node;

// Generated accessors retain Godot's instantiation rules without named lookup.
struct WGodotSceneProperty {
	Variant (*read)(const Node *);
	void (*write)(Node *, const Variant &);
};

struct WGodotSceneConnection {
	void (*connect)(Node *, Node *, const Array &, int);
};
