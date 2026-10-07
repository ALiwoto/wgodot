// wgodot-changes::file
#pragma once

class Node;

// Isolate resources written by animation tracks and rendering callbacks while
// sharing immutable assets. Preserve aliases between node fields and compositors.
void cinematic_isolate_preview_resources(Node *p_root);
