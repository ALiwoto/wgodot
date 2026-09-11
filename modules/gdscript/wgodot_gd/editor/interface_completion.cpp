// wgodot-changes::file

#include "interface_completion.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/wgodot_stdlib.h"

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
