// wgodot-changes::file
/**************************************************************************/
/*  wgodot_type_filter.cpp                                                */
/**************************************************************************/

#include "wgodot_type_filter.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/variant/variant.h"

namespace WGodotTypeFilter {

bool is_known(const String &p_filter) {
	if (p_filter.is_empty() || p_filter == "*" || ClassDB::class_exists(p_filter) || ScriptServer::is_global_class(p_filter)) {
		return true;
	}
	if (Variant::get_type_by_name(p_filter) < Variant::VARIANT_MAX) {
		return true;
	}
	const String lower = p_filter.to_lower();
	return lower == "variant" || lower == "void" || lower == "null" || lower == "signal" || lower == "enum" || lower == "class";
}

bool matches(const StringName &p_candidate, const StringName &p_filter) {
	if (p_filter == StringName("*") || String(p_candidate).nocasecmp_to(p_filter) == 0) {
		return true;
	}
	if (ClassDB::class_exists(p_candidate) && ClassDB::class_exists(p_filter)) {
		return ClassDB::is_parent_class(p_candidate, p_filter);
	}
	StringName type = p_candidate;
	while (ScriptServer::is_global_class(type)) {
		if (type == p_filter) {
			return true;
		}
		type = ScriptServer::get_global_class_base(type);
	}
	return ClassDB::class_exists(type) && ClassDB::class_exists(p_filter) && ClassDB::is_parent_class(type, p_filter);
}

} // namespace WGodotTypeFilter
