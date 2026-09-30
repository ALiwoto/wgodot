// wgodot-changes::file
#include "wgodot_resource_serialization.h"

#ifdef TOOLS_ENABLED
thread_local WGodotResourceSerialization *WGodotResourceSerialization::current = nullptr;

WGodotResourceSerialization::WGodotResourceSerialization(Rewrite p_callback, void *p_context) :
		previous(current), callback(p_callback), context(p_context) {
	current = this;
}

WGodotResourceSerialization::~WGodotResourceSerialization() {
	current = previous;
}

String WGodotResourceSerialization::rewrite(const String &p_value) {
	return current ? current->callback(current->context, p_value) : p_value;
}
#endif
