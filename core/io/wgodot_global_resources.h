// wgodot-changes::file
#pragma once

#include "core/io/resource.h"
#include "core/variant/variant_parser.h"

namespace WGlobalResources {
inline constexpr uint32_t BINARY_REFERENCE = 0x57475201;

struct Reference {
	StringName resource;
	StringName member;
	String text() const;
};

// Preserve symbolic references while saving a resource, except references back
// into that resource's own definitions. Nested saves restore the previous scope.
class SaveScope {
	SaveScope *previous;
	String source;
	static thread_local SaveScope *current;

public:
	explicit SaveScope(const Ref<Resource> &p_resource);
	~SaveScope();
	static bool reference(const Variant &p_value, Reference &r_reference);
};

void register_settings();
Error start();
void clear();
HashMap<StringName, String> paths();
// Native exports supply opaque resource paths instead of scanning directories.
void set_manifest(const HashMap<StringName, String> &p_paths);
Error resolve(const StringName &p_name, const StringName &p_member, Variant &r_value, String &r_error);
Variant value(const StringName &p_name, const StringName &p_member);
bool reference(const Variant &p_value, Reference &r_reference, const String &p_exclude = String());
Variant snapshot(const Variant &p_value);
Error parse(VariantParser::Stream *p_stream, int &r_line, String &r_error, Variant &r_value, bool p_syntax_only);
} // namespace WGlobalResources
