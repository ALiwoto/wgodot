// wgodot-changes::file
/**************************************************************************/
/*  wgodot_source_info.cpp                                                */
/**************************************************************************/

#include "wgodot_source_info.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "editor/doc/editor_help.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"
#include "modules/gdscript/language_server/gdscript_language_server.h"
#endif

namespace WGodotSourceInfo {

namespace {

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = "source_info";
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

void add_builtin_doc_status(Dictionary &r_response, bool p_is_deprecated, const String &p_deprecated_message, bool p_is_experimental, const String &p_experimental_message) {
	if (p_is_deprecated) {
		r_response["deprecated"] = p_deprecated_message.is_empty() ? DTR("This symbol may be changed or removed in future versions.") : DTR(p_deprecated_message).strip_edges();
	}
	if (p_is_experimental) {
		r_response["experimental"] = p_experimental_message.is_empty() ? DTR("This symbol may be changed or removed in future versions.") : DTR(p_experimental_message).strip_edges();
	}
}

String format_builtin_method_signature(const DocData::MethodDoc &p_method, bool p_signal = false) {
	String signature = p_method.name + "(";
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const DocData::ArgumentDoc &argument = p_method.arguments[i];
		signature += argument.name;
		if (!argument.type.is_empty()) {
			signature += ": " + argument.type;
		}
		if (!argument.default_value.is_empty()) {
			signature += " = " + argument.default_value;
		}
	}
	if (!p_method.rest_argument.name.is_empty()) {
		if (!p_method.arguments.is_empty()) {
			signature += ", ";
		}
		signature += "..." + p_method.rest_argument.name;
	}
	signature += ")";
	if (!p_signal) {
		signature += " -> " + (p_method.return_type.is_empty() ? String("Variant") : p_method.return_type);
	}
	return signature;
}

Dictionary make_builtin_doc_result(const String &p_target, const String &p_name, const String &p_kind, const String &p_declaring_class, const String &p_summary, const String &p_description) {
	Dictionary response;
	response["ok"] = true;
	response["command"] = "source_info";
	response["target"] = p_target;
	response["name"] = p_name;
	response["kind"] = p_kind;
	response["builtin"] = true;
	response["declaring_class"] = p_declaring_class;
	response["summary"] = p_summary;
	response["description"] = DTR(p_description).strip_edges();
	return response;
}

Dictionary resolve_builtin_target(const String &p_target) {
	const int separator = p_target.find_char('.');
	const String class_name = separator < 0 ? p_target : p_target.left(separator);
	const String member_name = separator < 0 ? String() : p_target.substr(separator + 1);
	const DocData::ClassDoc *class_doc = EditorHelp::get_doc(class_name);
	if (class_doc == nullptr || class_doc->is_script_doc) {
		return Dictionary();
	}

	if (member_name.is_empty()) {
		Dictionary response = make_builtin_doc_result(p_target, class_name, "builtin_class", class_name, "builtin class " + class_name, class_doc->description);
		response["brief_description"] = DTR(class_doc->brief_description).strip_edges();
		if (!class_doc->inherits.is_empty()) {
			response["inherits"] = class_doc->inherits;
		}
		add_builtin_doc_status(response, class_doc->is_deprecated, class_doc->deprecated_message, class_doc->is_experimental, class_doc->experimental_message);
		return response;
	}

	const DocData::ClassDoc *owner_doc = class_doc;
	while (owner_doc != nullptr) {
		for (const DocData::MethodDoc &method : owner_doc->methods) {
			if (method.name == member_name) {
				const String signature = format_builtin_method_signature(method);
				String summary = "builtin func " + signature;
				if (!method.qualifiers.is_empty()) {
					summary += " [" + method.qualifiers + "]";
				}
				Dictionary response = make_builtin_doc_result(p_target, method.name, "builtin_func", owner_doc->name, summary, method.description);
				response["signature"] = signature;
				response["qualifiers"] = method.qualifiers;
				if (!method.errors_returned.is_empty()) {
					Array errors;
					for (int error : method.errors_returned) {
						errors.push_back(error);
					}
					response["errors_returned"] = errors;
				}
				add_builtin_doc_status(response, method.is_deprecated, method.deprecated_message, method.is_experimental, method.experimental_message);
				return response;
			}
		}
		for (const DocData::MethodDoc &signal : owner_doc->signals) {
			if (signal.name == member_name) {
				const String signature = format_builtin_method_signature(signal, true);
				Dictionary response = make_builtin_doc_result(p_target, signal.name, "builtin_signal", owner_doc->name, "builtin signal " + signature, signal.description);
				response["signature"] = signature;
				add_builtin_doc_status(response, signal.is_deprecated, signal.deprecated_message, signal.is_experimental, signal.experimental_message);
				return response;
			}
		}
		for (const DocData::PropertyDoc &property : owner_doc->properties) {
			if (property.name == member_name) {
				String signature = property.name + ": " + property.type;
				if (!property.default_value.is_empty()) {
					signature += " = " + property.default_value;
				}
				Dictionary response = make_builtin_doc_result(p_target, property.name, "builtin_var", owner_doc->name, "builtin var " + signature, property.description);
				response["signature"] = signature;
				add_builtin_doc_status(response, property.is_deprecated, property.deprecated_message, property.is_experimental, property.experimental_message);
				return response;
			}
		}
		for (const DocData::ConstantDoc &constant : owner_doc->constants) {
			if (constant.name == member_name) {
				String signature = constant.name;
				if (!constant.type.is_empty()) {
					signature += ": " + constant.type;
				}
				if (constant.is_value_valid) {
					signature += " = " + constant.value;
				}
				Dictionary response = make_builtin_doc_result(p_target, constant.name, "builtin_const", owner_doc->name, "builtin const " + signature, constant.description);
				response["signature"] = signature;
				add_builtin_doc_status(response, constant.is_deprecated, constant.deprecated_message, constant.is_experimental, constant.experimental_message);
				return response;
			}
		}
		const DocData::EnumDoc *enum_doc = owner_doc->enums.getptr(member_name);
		if (enum_doc != nullptr) {
			Dictionary response = make_builtin_doc_result(p_target, member_name, "builtin_enum", owner_doc->name, "builtin enum " + member_name, enum_doc->description);
			add_builtin_doc_status(response, enum_doc->is_deprecated, enum_doc->deprecated_message, enum_doc->is_experimental, enum_doc->experimental_message);
			return response;
		}
		owner_doc = owner_doc->inherits.is_empty() ? nullptr : EditorHelp::get_doc(owner_doc->inherits);
	}

	return make_error("builtin_member_not_found", "Built-in Godot member was not found: " + p_target);
}

#ifdef MODULE_GDSCRIPT_ENABLED
String get_identifier_kind(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return "class";
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return "const";
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return "func";
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return "signal";
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable->is_static ? "static_var" : "var";
		case GDScriptParser::ClassNode::Member::ENUM:
			return "enum";
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return "enum_value";
		case GDScriptParser::ClassNode::Member::GROUP:
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			return "unknown";
	}
	return "unknown";
}

const GDScriptParser::IdentifierNode *get_member_identifier(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return p_member.m_class->identifier;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return p_member.constant->identifier;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return p_member.function->identifier;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return p_member.signal->identifier;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable->identifier;
		case GDScriptParser::ClassNode::Member::ENUM:
			return p_member.m_enum->identifier;
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return p_member.enum_value.identifier;
		case GDScriptParser::ClassNode::Member::GROUP:
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			return nullptr;
	}
	return nullptr;
}

const GDScriptParser::ClassNode *find_class(const GDScriptParser::ClassNode *p_class, const String &p_fqcn) {
	if (p_class == nullptr) {
		return nullptr;
	}
	if (p_class->fqcn == p_fqcn) {
		return p_class;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			if (const GDScriptParser::ClassNode *found = find_class(member.m_class, p_fqcn)) {
				return found;
			}
		}
	}
	return nullptr;
}

Dictionary make_source_result(const String &p_target, const String &p_path, const String &p_name, const String &p_kind, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return make_error("source_position_unavailable", "The declaration does not have a renameable source identifier: " + p_target);
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "source_info";
	response["target"] = p_target;
	response["name"] = p_name;
	response["kind"] = p_kind;
	response["path"] = p_path;
	response["absolute_path"] = ProjectSettings::get_singleton()->globalize_path(p_path);
	response["line"] = p_identifier->start_line - 1;
	response["character"] = p_identifier->start_column - 1;
	response["display_line"] = p_identifier->start_line;
	response["display_column"] = p_identifier->start_column;
	return response;
}

Dictionary resolve_in_class(const String &p_target, const Ref<GDScript> &p_script, ExtendGDScriptParser &p_parser, const GDScriptParser::ClassNode *p_class, const PackedStringArray &p_parts, int p_part_index, const StringName &p_new_name = StringName()) {
	if (p_class == nullptr || p_part_index < 0 || p_part_index >= p_parts.size()) {
		return make_error("source_not_found", "Could not resolve the source declaration for: " + p_target);
	}

	const StringName member_name = p_parts[p_part_index];
	const int *member_index = p_class->members_indices.getptr(member_name);
	if (member_index == nullptr) {
		if (p_class->base_type.kind == GDScriptParser::DataType::CLASS && p_class->base_type.class_type != nullptr) {
			return resolve_in_class(p_target, p_script, p_parser, p_class->base_type.class_type, p_parts, p_part_index, p_new_name);
		}
		if (p_class->base_type.kind == GDScriptParser::DataType::SCRIPT && p_class->base_type.script_type.is_valid()) {
			Ref<GDScript> typed_base = Ref<GDScript>(Object::cast_to<GDScript>(p_class->base_type.script_type.ptr()));
			if (typed_base.is_valid()) {
				const String typed_base_path = typed_base->get_script_path().get_slice("::", 0);
				ExtendGDScriptParser typed_base_parser;
				typed_base_parser.parse(typed_base->get_source_code(), typed_base_path);
				if (typed_base_parser.parse_result == OK) {
					const GDScriptParser::ClassNode *typed_base_class = find_class(typed_base_parser.get_tree(), typed_base->get_fully_qualified_name());
					return resolve_in_class(p_target, typed_base, typed_base_parser, typed_base_class, p_parts, p_part_index, p_new_name);
				}
			}
		}
		Ref<GDScript> base = p_script->get_base();
		if (base.is_valid()) {
			const String base_path = base->get_script_path().get_slice("::", 0);
			ExtendGDScriptParser base_parser;
			base_parser.parse(base->get_source_code(), base_path);
			if (base_parser.parse_result == OK) {
				const GDScriptParser::ClassNode *base_class = find_class(base_parser.get_tree(), base->get_fully_qualified_name());
				return resolve_in_class(p_target, base, base_parser, base_class, p_parts, p_part_index, p_new_name);
			}
		}
		return make_error("source_not_found", "Member was not found while resolving: " + p_target);
	}

	const GDScriptParser::ClassNode::Member &member = p_class->members[*member_index];
	if (p_part_index == p_parts.size() - 1) {
		if (!p_new_name.is_empty() && p_new_name != member_name && p_class->members_indices.has(p_new_name)) {
			return make_error("rename_collision", "The declaration scope already contains a member named: " + String(p_new_name));
		}
		return make_source_result(p_target, p_parser.get_path(), member.get_name(), get_identifier_kind(member), get_member_identifier(member));
	}

	const GDScriptParser::DataType datatype = member.get_datatype();
	if (datatype.kind == GDScriptParser::DataType::CLASS && datatype.class_type != nullptr) {
		return resolve_in_class(p_target, p_script, p_parser, datatype.class_type, p_parts, p_part_index + 1, p_new_name);
	}
	if (datatype.kind == GDScriptParser::DataType::SCRIPT && datatype.script_type.is_valid()) {
		Ref<GDScript> next_script = Ref<GDScript>(Object::cast_to<GDScript>(datatype.script_type.ptr()));
		if (next_script.is_valid()) {
			const String next_path = next_script->get_script_path().get_slice("::", 0);
			ExtendGDScriptParser next_parser;
			next_parser.parse(next_script->get_source_code(), next_path);
			if (next_parser.parse_result != OK) {
				return make_error("script_parse_failed", "Could not analyze the declared type while resolving: " + p_target);
			}
			const GDScriptParser::ClassNode *next_class = find_class(next_parser.get_tree(), next_script->get_fully_qualified_name());
			return resolve_in_class(p_target, next_script, next_parser, next_class, p_parts, p_part_index + 1, p_new_name);
		}
	}
	return make_error("type_unresolved", "A declared GDScript type is required to resolve the nested member: " + p_target);
}

Dictionary resolve_target(const String &p_target, const StringName &p_new_name = StringName()) {
	String script_path;
	PackedStringArray member_parts;
	String class_name;
	bool target_is_class = false;

	if (p_target.begins_with("res://") || p_target.begins_with("user://")) {
		const int separator = p_target.find("::");
		script_path = separator < 0 ? p_target : p_target.left(separator);
		if (separator >= 0) {
			member_parts = p_target.substr(separator + 2).split(".", false);
		} else {
			target_is_class = true;
		}
	} else {
		const PackedStringArray parts = p_target.split(".", false);
		if (parts.is_empty()) {
			return make_error("invalid_target", "A named GDScript class or script path is required.");
		}
		class_name = parts[0];
		script_path = ScriptServer::get_global_class_path(class_name);
		if (script_path.is_empty()) {
			return make_error("class_not_found", "Named GDScript class was not found: " + class_name);
		}
		for (int i = 1; i < parts.size(); i++) {
			member_parts.push_back(parts[i]);
		}
		target_is_class = member_parts.is_empty();
	}

	Ref<GDScript> script = ResourceLoader::load(script_path);
	if (script.is_null()) {
		return make_error("script_load_failed", "Could not load GDScript: " + script_path);
	}
	const String source_path = script_path.get_slice("::", 0);
	ExtendGDScriptParser parser;
	parser.parse(script->get_source_code(), source_path);
	if (parser.parse_result != OK) {
		return make_error("script_parse_failed", "Could not analyze GDScript: " + source_path);
	}
	const GDScriptParser::ClassNode *script_class = find_class(parser.get_tree(), script->get_fully_qualified_name());
	if (script_class == nullptr) {
		script_class = parser.get_tree();
	}
	if (target_is_class) {
		if (!p_new_name.is_empty() && p_new_name != class_name && ScriptServer::is_global_class(p_new_name)) {
			return make_error("rename_collision", "A global GDScript class already uses the name: " + String(p_new_name));
		}
		return make_source_result(p_target, source_path, class_name.is_empty() ? String(script_class->identifier ? script_class->identifier->name : StringName()) : class_name, "class", script_class->identifier);
	}
	return resolve_in_class(p_target, script, parser, script_class, member_parts, 0, p_new_name);
}

Dictionary resolve_position(const String &p_path, int p_line, int p_character) {
	if (p_line < 0 || p_character < 0) {
		return make_error("invalid_source_position", "Source line and column must be non-negative LSP coordinates.");
	}
	String resource_path = p_path;
	if (!resource_path.begins_with("res://") && !resource_path.begins_with("user://")) {
		resource_path = ProjectSettings::get_singleton()->localize_path(resource_path);
	}
	if (!resource_path.begins_with("res://") || resource_path.get_extension().to_lower() != "gd") {
		return make_error("invalid_source_path", "The source position must point to a project .gd file.");
	}
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(resource_path, &read_error);
	if (read_error != OK) {
		return make_error("source_read_failed", "Could not read GDScript: " + resource_path);
	}
	ExtendGDScriptParser parser;
	parser.parse(source, resource_path);
	if (parser.parse_result != OK) {
		return make_error("script_parse_failed", "Could not analyze GDScript: " + resource_path);
	}
	LSP::Range range;
	const String name = parser.get_symbol_name_under_position(LSP::Position(p_line, p_character), range);
	if (name.is_empty()) {
		return make_error("source_not_found", "No symbol was found at the supplied source position.");
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "source_info";
	response["target"] = resource_path + ":" + itos(p_line + 1) + ":" + itos(p_character + 1);
	response["name"] = name;
	response["kind"] = "symbol";
	response["path"] = resource_path;
	response["absolute_path"] = ProjectSettings::get_singleton()->globalize_path(resource_path);
	response["line"] = range.start.line;
	response["character"] = range.start.character;
	response["display_line"] = range.start.line + 1;
	response["display_column"] = range.start.character + 1;
	return response;
}
#endif

void add_lsp_endpoint(Dictionary &r_response) {
#ifdef MODULE_GDSCRIPT_ENABLED
	r_response["lsp_host"] = String(EditorSettings::get_singleton()->get_setting("network/language_server/remote_host"));
	r_response["lsp_port"] = GDScriptLanguageServer::port_override > -1 ? GDScriptLanguageServer::port_override : (int)EditorSettings::get_singleton()->get_setting("network/language_server/remote_port");
#endif
	r_response["project_root"] = ProjectSettings::get_singleton()->get_resource_path();
}

} // namespace

Dictionary resolve(const Dictionary &p_options) {
	const String target = String(p_options.get("target", String())).strip_edges();
	if (target.is_empty()) {
		return make_error("invalid_target", "source_info requires a target.");
	}
	if (!target.begins_with("res://") && !target.begins_with("user://")) {
		const String class_name = target.get_slice(".", 0);
		if (!ScriptServer::is_global_class(class_name)) {
			const Dictionary builtin_result = resolve_builtin_target(target);
			if (!builtin_result.is_empty()) {
				return builtin_result;
			}
		}
	}
#ifndef MODULE_GDSCRIPT_ENABLED
	return make_error("gdscript_unavailable", "This editor was built without the GDScript module.");
#else
	return resolve_target(target);
#endif
}

Dictionary rename_preflight(const Dictionary &p_options) {
	if (ScriptEditor::get_singleton() != nullptr) {
		const PackedStringArray unsaved_files = ScriptEditor::get_singleton()->get_unsaved_files();
		if (!unsaved_files.is_empty()) {
			Dictionary response = make_error("unsaved_editor_files", "Save all scripts open in the Godot editor before renaming.");
			response["files"] = unsaved_files;
			return response;
		}
	}

#ifndef MODULE_GDSCRIPT_ENABLED
	return make_error("gdscript_unavailable", "This editor was built without the GDScript module.");
#else
	Dictionary response;
	if (p_options.has("target")) {
		response = resolve_target(String(p_options["target"]), StringName(String(p_options.get("new_name", String()))));
	} else {
		response = resolve_position(String(p_options.get("path", String())), (int)p_options.get("line", -1), (int)p_options.get("character", -1));
	}
	if (!(bool)response.get("ok", false)) {
		return response;
	}
	add_lsp_endpoint(response);
	return response;
#endif
}

Dictionary rename_complete() {
	if (EditorFileSystem::get_singleton() != nullptr) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
	if (ScriptEditor::get_singleton() != nullptr) {
		ScriptEditor::get_singleton()->reload_open_files();
	}
	Dictionary response;
	response["ok"] = true;
	response["command"] = "rename_complete";
	return response;
}

} // namespace WGodotSourceInfo
