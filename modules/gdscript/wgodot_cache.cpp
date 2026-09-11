// wgodot-changes::file

#include "gdscript_cache.h"

#ifdef TOOLS_ENABLED
void GDScriptParserRef::initialize_export_source(const String &p_path, const String &p_source) {
	path = p_path;
	export_source = true;
	export_source_text = p_source;
	abandoned = true; // This reference never belongs to the editor parser cache.
}
#endif
