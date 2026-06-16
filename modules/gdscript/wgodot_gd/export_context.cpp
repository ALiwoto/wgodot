// wgodot-changes::file
/**************************************************************************/
/*  export_context.cpp                                                    */
/**************************************************************************/

#include "export_context.h"

#include "obfuscation_names.h"

#include "core/object/script_language.h"
#include "core/templates/local_vector.h"

namespace {

String get_class_primary_key(const GDScriptParser::ClassNode *p_class, const String &p_script_path) {
	if (p_class == nullptr) {
		return String();
	}
	if (!p_class->fqcn.is_empty()) {
		return p_class->fqcn;
	}
	if (p_class->outer == nullptr && !p_script_path.is_empty()) {
		return p_script_path;
	}
	if (p_class->identifier != nullptr && !p_class->identifier->name.is_empty()) {
		return String(p_class->identifier->name);
	}
	return String();
}

void index_class(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ClassNode *p_class, const String &p_script_path, bool p_no_mangle_scope) {
	if (p_class == nullptr) {
		return;
	}

	r_context.reserve_script_global_class_name(p_class);

	const bool no_mangle_scope = p_no_mangle_scope || p_class->wgodot_no_mangle;
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		const String member_name = member.get_name();
		if (!member_name.is_empty()) {
			r_context.reserve_member_name(StringName(member_name));
		}
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			index_class(r_context, member.m_class, p_script_path, no_mangle_scope);
			continue;
		}

		if (no_mangle_scope) {
			continue;
		}

		StringName member_name;
		if (member.type == GDScriptParser::ClassNode::Member::FUNCTION &&
				member.function != nullptr &&
				member.function->identifier != nullptr &&
				member.function->wgodot_obfuscate &&
				!member.function->wgodot_no_mangle) {
			member_name = member.function->identifier->name;
		} else if (member.type == GDScriptParser::ClassNode::Member::VARIABLE &&
				member.variable != nullptr &&
				member.variable->identifier != nullptr &&
				member.variable->wgodot_obfuscate &&
				!member.variable->wgodot_no_mangle) {
			member_name = member.variable->identifier->name;
		} else {
			continue;
		}

		Vector<String> keys;
		WGodotGDScriptExportTransform::ExportContext::make_member_keys(p_class, p_script_path, member_name, keys);
		if (keys.is_empty()) {
			continue;
		}

		const String obfuscated_name = r_context.get_or_create_member_rename(keys[0]);
		for (int i = 1; i < keys.size(); i++) {
			r_context.bind_member_rename(keys[i], obfuscated_name);
		}
	}
}

} // namespace

namespace WGodotGDScriptExportTransform {

void ExportContext::reset() {
	member_renames.clear();
	reserved_member_names.clear();
	reserved_global_class_names.clear();
	reserve_registered_global_class_names();
	obfuscation_random.randomize();
}

void ExportContext::set_options(const TransformOptions &p_options) {
	options = p_options;
}

void ExportContext::reserve_member_name(const StringName &p_name) {
	if (!p_name.is_empty()) {
		reserved_member_names.insert(p_name);
	}
}

void ExportContext::reserve_global_class_name(const StringName &p_name) {
	if (p_name.is_empty()) {
		return;
	}

	reserved_global_class_names.insert(p_name);
	reserved_member_names.insert(p_name);
}

void ExportContext::reserve_script_global_class_name(const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr ||
			p_class->outer != nullptr ||
			p_class->identifier == nullptr ||
			p_class->identifier->name.is_empty() ||
			p_class->fqcn.begins_with("res://")) {
		return;
	}

	reserve_global_class_name(p_class->identifier->name);
}

void ExportContext::reserve_registered_global_class_names() {
	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	for (const StringName &global_class : global_classes) {
		reserve_global_class_name(global_class);
	}
}

void ExportContext::seed_reserved_obfuscated_names(HashSet<StringName> &r_reserved_names) const {
	for (const StringName &global_class : reserved_global_class_names) {
		r_reserved_names.insert(global_class);
	}
}

String ExportContext::make_obfuscated_name(HashSet<StringName> &r_reserved_names, const String &p_warning_context) {
	seed_reserved_obfuscated_names(r_reserved_names);
	return WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, r_reserved_names, p_warning_context);
}

String ExportContext::make_member_key(const String &p_class_key, const StringName &p_member_name) {
	if (p_class_key.is_empty() || p_member_name.is_empty()) {
		return String();
	}

	return p_class_key + "::" + String(p_member_name);
}

void ExportContext::make_member_keys(const GDScriptParser::ClassNode *p_class, const String &p_script_path, const StringName &p_member_name, Vector<String> &r_keys) {
	const String primary_key = get_class_primary_key(p_class, p_script_path);
	const String primary_member_key = make_member_key(primary_key, p_member_name);
	if (!primary_member_key.is_empty()) {
		r_keys.push_back(primary_member_key);
	}

	if (p_class != nullptr && p_class->outer == nullptr && !p_script_path.is_empty() && primary_key != p_script_path) {
		const String script_member_key = make_member_key(p_script_path, p_member_name);
		if (!script_member_key.is_empty()) {
			r_keys.push_back(script_member_key);
		}
	}
}

void ExportContext::index_script(const GDScriptParser::ClassNode *p_class, const String &p_script_path) {
	index_class(*this, p_class, p_script_path, false);
}

String ExportContext::get_or_create_member_rename(const String &p_key) {
	if (p_key.is_empty()) {
		return String();
	}

	const String *existing = member_renames.getptr(p_key);
	if (existing != nullptr) {
		return *existing;
	}

	const String obfuscated_name = make_obfuscated_name(reserved_member_names, "member name");
	member_renames[p_key] = obfuscated_name;
	return obfuscated_name;
}

void ExportContext::bind_member_rename(const String &p_key, const String &p_obfuscated_name) {
	if (p_key.is_empty() || p_obfuscated_name.is_empty() || member_renames.has(p_key)) {
		return;
	}

	member_renames[p_key] = p_obfuscated_name;
	reserved_member_names.insert(StringName(p_obfuscated_name));
}

const String *ExportContext::get_member_rename(const String &p_key) const {
	if (p_key.is_empty()) {
		return nullptr;
	}

	return member_renames.getptr(p_key);
}

} // namespace WGodotGDScriptExportTransform
