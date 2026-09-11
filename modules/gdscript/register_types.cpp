/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "gdscript.h"
#include "gdscript_cache.h"
#include "gdscript_parser.h"
#include "gdscript_resource_format.h"
#include "gdscript_tokenizer_buffer.h"
#include "gdscript_utility_functions.h"
// wgodot-changes::begin
#include "wgodot_gd/builtin_class_aliases.h"
#ifdef TOOLS_ENABLED
#include "wgodot_gd/editor/export/export_pipeline.h"
#include "wgodot_gd/editor/export/export_maps.h"
#endif
#include "wgodot_gd/interface_method_aliases.h"
#include "wgodot_gd/string_obfuscation.h"
#include "wgodot_stdlib.h"
// wgodot-changes::end

#ifdef TOOLS_ENABLED
#include "editor/gdscript_editor_language.h"
#include "editor/gdscript_highlighter.h"
#include "editor/gdscript_translation_parser_plugin.h"
#include "editor/script/script_editor_plugin.h"

#ifndef GDSCRIPT_NO_LSP
#include "language_server/gdscript_language_protocol.h"
#include "language_server/gdscript_language_server.h"
#endif
#endif // TOOLS_ENABLED

#ifdef TESTS_ENABLED
#include "tests/test_gdscript.h"
#endif

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/translations/editor_translation_parser.h"

#ifndef GDSCRIPT_NO_LSP
#include "core/config/engine.h"
#endif
#endif // TOOLS_ENABLED

#ifdef TESTS_ENABLED
#include "tests/test_macros.h"
#endif

GDScriptLanguage *script_language_gd = nullptr;
Ref<ResourceFormatLoaderGDScript> resource_loader_gd;
Ref<ResourceFormatSaverGDScript> resource_saver_gd;
GDScriptCache *gdscript_cache = nullptr;

#ifdef TOOLS_ENABLED

Ref<GDScriptEditorTranslationParserPlugin> gdscript_translation_parser_plugin;

class GDScriptExportPlugin : public EditorExportPlugin {
	GDSOFTCLASS(GDScriptExportPlugin, EditorExportPlugin);

	static constexpr EditorExportPreset::ScriptExportMode DEFAULT_SCRIPT_MODE = EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;
	EditorExportPreset::ScriptExportMode script_mode = DEFAULT_SCRIPT_MODE;
	// wgodot-changes::begin
	WGodotGDScriptExportTransform::ExportPipeline pipeline;
	WGodotGDScriptExportTransform::TransformOptions transform_options;
	String diagnostic_map_path;
	// wgodot-changes::end

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override {
		script_mode = DEFAULT_SCRIPT_MODE;
		// wgodot-changes::begin
		pipeline.reset();
		// wgodot-changes::end

		const Ref<EditorExportPreset> &preset = get_export_preset();
		if (preset.is_valid()) {
			script_mode = preset->get_script_export_mode();
		}
		// wgodot-changes::begin
		WGodotGDScriptExportTransform::TransformOptions options = WGodotGDScriptExportTransform::setup_params();
		options.redact_diagnostics = options.redact_diagnostics && !p_debug;
		options.binary_tokens_export = script_mode != EditorExportPreset::MODE_SCRIPT_TEXT;
		transform_options = options;
		diagnostic_map_path = p_path.get_basename() + ".diagnostics.json";
		// wgodot-changes::end
	}

	// wgodot-changes::begin
	virtual void _export_paths_ready(const HashSet<String> &p_paths) override {
		pipeline.prepare_export(this, p_paths, transform_options);
	}

	virtual void _export_global_class_list(Array &r_global_class_list) override {
		WGodotGDScriptExportTransform::transform_global_class_list(&pipeline.get_artifacts(), &r_global_class_list);
	}

	virtual Error _export_completed() override {
		return pipeline.complete_export(this, transform_options.redact_diagnostics, diagnostic_map_path);
	}

	virtual void _export_end() override {
		pipeline.reset();
	}
	// wgodot-changes::end

	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		// wgodot-changes::begin
		pipeline.export_file(this, p_path, script_mode);
		// wgodot-changes::end
	}

public:
	virtual String get_name() const override { return "GDScript"; }
};

static void _editor_init() {
	Ref<GDScriptExportPlugin> gd_export;
	gd_export.instantiate();
	EditorExport::get_singleton()->add_export_plugin(gd_export);

#ifdef TOOLS_ENABLED
	Ref<GDScriptSyntaxHighlighter> gdscript_syntax_highlighter;
	gdscript_syntax_highlighter.instantiate();
	ScriptEditor::get_singleton()->register_syntax_highlighter(gdscript_syntax_highlighter);
#endif
}

#endif // TOOLS_ENABLED

void initialize_gdscript_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		// wgodot-changes::begin
		WGodotGDScriptBuiltinClassAliases::clear_runtime_cache();
		WGodotGDScriptInterfaceMethodAliases::clear_runtime_cache();
		WGodotGDScriptStringObfuscation::clear_runtime_cache();
		// wgodot-changes::end

		GDREGISTER_CLASS(GDScript);
		GDREGISTER_INTERNAL_CLASS(GDScriptFunctionState);

		script_language_gd = memnew(GDScriptLanguage);
		ScriptServer::register_language(script_language_gd);

		resource_loader_gd.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_gd);

		resource_saver_gd.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_gd);

		gdscript_cache = memnew(GDScriptCache);

		GDScriptUtilityFunctions::register_functions();
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		EditorNode::add_init_callback(_editor_init);

		gdscript_translation_parser_plugin.instantiate();
		EditorTranslationParser::get_singleton()->add_parser(gdscript_translation_parser_plugin, EditorTranslationParser::STANDARD);
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		memnew(GDScriptEditorLanguage);

		GDREGISTER_CLASS(GDScriptSyntaxHighlighter);
#ifndef GDSCRIPT_NO_LSP
		register_lsp_types();
		memnew(GDScriptLanguageProtocol);
		EditorPlugins::add_by_type<GDScriptLanguageServer>();

		Engine::Singleton singleton("GDScriptLanguageProtocol", GDScriptLanguageProtocol::get_singleton());
		singleton.editor_only = true;
		Engine::get_singleton()->add_singleton(singleton);
#endif // !GDSCRIPT_NO_LSP
	}
#endif // TOOLS_ENABLED
}

void uninitialize_gdscript_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		ScriptServer::unregister_language(script_language_gd);

		memdelete(gdscript_cache);
		memdelete(script_language_gd);

		ResourceLoader::remove_resource_format_loader(resource_loader_gd);
		resource_loader_gd.unref();

		ResourceSaver::remove_resource_format_saver(resource_saver_gd);
		resource_saver_gd.unref();

		GDScriptParser::cleanup();
		// wgodot-changes::begin
		WGodotGDScriptStdLib::clear_module_interfaces();
		WGodotGDScriptBuiltinClassAliases::clear_runtime_cache();
		WGodotGDScriptInterfaceMethodAliases::clear_runtime_cache();
		WGodotGDScriptStringObfuscation::clear_runtime_cache();
		// wgodot-changes::end
		GDScriptUtilityFunctions::unregister_functions();
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorTranslationParser::get_singleton()->remove_parser(gdscript_translation_parser_plugin, EditorTranslationParser::STANDARD);
		gdscript_translation_parser_plugin.unref();
#ifndef GDSCRIPT_NO_LSP
		memdelete(GDScriptLanguageProtocol::get_singleton());
#endif // GDSCRIPT_NO_LSP
		memdelete(GDScriptEditorLanguage::get_singleton());
	}
#endif // TOOLS_ENABLED
}

#ifdef TESTS_ENABLED
void test_tokenizer() {
	GDScriptTests::test(GDScriptTests::TestType::TEST_TOKENIZER);
}

void test_tokenizer_buffer() {
	GDScriptTests::test(GDScriptTests::TestType::TEST_TOKENIZER_BUFFER);
}

void test_parser() {
	GDScriptTests::test(GDScriptTests::TestType::TEST_PARSER);
}

void test_compiler() {
	GDScriptTests::test(GDScriptTests::TestType::TEST_COMPILER);
}

void test_bytecode() {
	GDScriptTests::test(GDScriptTests::TestType::TEST_BYTECODE);
}

REGISTER_TEST_COMMAND("gdscript-tokenizer", &test_tokenizer);
REGISTER_TEST_COMMAND("gdscript-tokenizer-buffer", &test_tokenizer_buffer);
REGISTER_TEST_COMMAND("gdscript-parser", &test_parser);
REGISTER_TEST_COMMAND("gdscript-compiler", &test_compiler);
REGISTER_TEST_COMMAND("gdscript-bytecode", &test_bytecode);
#endif
