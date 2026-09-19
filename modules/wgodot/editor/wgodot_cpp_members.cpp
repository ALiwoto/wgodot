// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "core/variant/wgodot_native_members.h"

String WGodotCppEmitter::builtin_member(Variant::Type p_type, const StringName &p_name, const GDScriptParser::Node *p_origin, const String &p_receiver, const String &p_value) {
	const char *accessor = WGodotNativeMembers::get_accessor(p_type, p_name);
	if (!accessor) {
		unsupported(p_origin, "native member access to " + Variant::get_type_name(p_type) + "." + String(p_name));
		return String();
	}
	class_call_headers.insert("core/variant/variant_setget.h");
	return String(accessor) + (p_value.is_empty() ? "::wgodot_get(" + p_receiver : "::wgodot_set(" + p_receiver + ", " + p_value) + ")";
}
