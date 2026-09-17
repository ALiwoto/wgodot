// wgodot-changes::file

#include "interface_completion.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/wgodot_stdlib.h"

void WGodotGDScriptEditor::find_native_interface_members(const StringName &p_type, bool p_static, bool p_only_functions, bool p_types_only, bool p_add_braces, HashMap<String, EditorLanguage::CompletionOption> &r_result) {
	const auto *contract = WGodotNativeInterfaces::get_descriptor(p_type);
	if (!contract) {
		return;
	}
	auto add = [&](const StringName &p_name, EditorLanguage::CompletionKind p_kind, int p_arguments = -1) {
		EditorLanguage::CompletionOption option(p_name, p_kind, EditorLanguage::CompletionLocation::OTHER);
		if (p_add_braces && p_arguments >= 0) {
			option.insert_text += p_arguments ? "(" : "()";
			option.display += p_arguments ? U"(\u2026)" : U"()";
		}
		r_result.insert(option.display, option);
	};
	if (!p_only_functions) {
		for (const auto &entry : contract->enums) {
			add(entry.key, EditorLanguage::CompletionKind::ENUM);
		}
		if (!p_types_only) {
			for (const auto &entry : contract->constants) {
				add(entry.key, EditorLanguage::CompletionKind::CONSTANT);
			}
			if (!p_static) {
				for (const auto &entry : contract->properties) {
					add(entry.key, EditorLanguage::CompletionKind::MEMBER_VARIABLE);
				}
				for (const auto &entry : contract->signals) {
					add(entry.key, EditorLanguage::CompletionKind::SIGNAL);
				}
			}
		}
	}
	if (!p_static && !p_types_only) {
		for (const auto &entry : contract->methods) {
			add(entry.key, EditorLanguage::CompletionKind::FUNCTION, entry.value.arguments.size());
		}
	}
}

void WGodotGDScriptEditor::find_builtin_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result) {
	LocalVector<StringName> interfaces;
	WGodotGDScriptStdLib::get_global_interface_list(interfaces);
	for (const StringName &name : interfaces) {
		EditorLanguage::CompletionOption option(name, EditorLanguage::CompletionKind::CLASS, EditorLanguage::CompletionLocation::OTHER_USER_CODE);
		r_result.insert(option.display, option);
	}
}

void WGodotGDScriptEditor::find_interfaces(HashMap<String, EditorLanguage::CompletionOption> &r_result) {
	LocalVector<StringName> classes;
	ScriptServer::get_global_class_list(classes);
	for (const StringName &name : classes) {
		if (ScriptServer::get_global_class_language(name) != SNAME("GDScript")) {
			continue;
		}
		Error error = OK;
		Ref<GDScriptParserRef> parser = GDScriptCache::get_parser(ScriptServer::get_global_class_path(name), GDScriptParserRef::PARSED, error);
		if (error == OK && parser->get_parser()->get_tree()->wgodot_is_interface) {
			EditorLanguage::CompletionOption option(name, EditorLanguage::CompletionKind::CLASS, EditorLanguage::CompletionLocation::OTHER_USER_CODE);
			r_result.insert(option.display, option);
		}
	}
	find_builtin_interfaces(r_result);
}
