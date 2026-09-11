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
		String details;
		Error error = pipeline.prepare(p_paths, transform_options, details);
		if (error != OK) {
			set_export_error(error, details);
			return;
		}
		const auto &artifacts = pipeline.get_artifacts();
		const Vector<uint8_t> builtin_class_aliases = WGodotGDScriptBuiltinClassAliases::serialize_alias_map(artifacts);
		if (!builtin_class_aliases.is_empty()) {
			add_file(WGodotGDScriptBuiltinClassAliases::get_alias_map_path(), builtin_class_aliases, false);
		}
		const Vector<uint8_t> interface_method_aliases = WGodotGDScriptInterfaceMethodAliases::serialize_alias_map(artifacts);
		if (!interface_method_aliases.is_empty()) {
			add_file(WGodotGDScriptInterfaceMethodAliases::get_alias_map_path(), interface_method_aliases, false);
		}
		const Vector<uint8_t> string_map = WGodotGDScriptStringObfuscation::serialize_string_map(artifacts);
		if (!string_map.is_empty()) {
			add_file(WGodotGDScriptStringObfuscation::get_string_map_path(), string_map, false);
		}
	}

	virtual void _export_global_class_list(Array &r_global_class_list) override {
		WGodotGDScriptExportTransform::transform_global_class_list(&pipeline.get_artifacts(), &r_global_class_list);
	}

	virtual Error _export_completed() override {
		if (transform_options.redact_diagnostics) {
			const Error error = pipeline.get_artifacts().get_diagnostics().save_map(diagnostic_map_path);
			if (error != OK) {
				set_export_error(error, "Cannot write diagnostic map: " + diagnostic_map_path);
				return error;
			}
		}
		return OK;
	}

	virtual void _export_end() override {
		pipeline.reset();
	}
	// wgodot-changes::end

	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		// wgodot-changes::begin
		// Private maps from earlier exports must not enter a later package through
		// resource include filters, even when redaction is currently disabled.
		if (p_path.ends_with(".diagnostics.json")) {
			skip();
			return;
		}
		if (p_path.get_extension() != "gd") {
			return;
		}
		const auto *prepared = pipeline.get_source(p_path);
		if (prepared == nullptr) {
			set_export_error(ERR_INVALID_DATA, "Missing prepared GDScript export: " + p_path);
			return;
		}
		const String &source = prepared->get_text();
		const auto &artifacts = pipeline.get_artifacts();
		const String obfuscated_script_path = artifacts.get_exported_script_path(p_path);
		const bool script_path_changed = !obfuscated_script_path.is_empty();
		if (script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
			add_file(script_path_changed ? obfuscated_script_path : p_path, source.to_utf8_buffer(), false);
			skip();
			return;
		}
		// wgodot-changes::end
		GDScriptTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? GDScriptTokenizerBuffer::COMPRESS_ZSTD : GDScriptTokenizerBuffer::COMPRESS_NONE;
		// wgodot-changes::begin
		Vector<uint8_t> file = GDScriptTokenizerBuffer::parse_code_string(source, compress_mode);
		// wgodot-changes::end
		if (file.is_empty()) {
			// wgodot-changes::begin
			set_export_error(ERR_PARSE_ERROR, "Cannot tokenize prepared GDScript export: " + p_path);
			// wgodot-changes::end
			return;
		}

		if (script_path_changed) {
			const String obfuscated_binary_script_path = artifacts.get_exported_binary_script_path(p_path);
			const String remap_source = "[remap]\n\npath=\"" + obfuscated_binary_script_path.c_escape() + "\"\n";
			add_file(obfuscated_binary_script_path, file, false);
			add_file(obfuscated_script_path + ".remap", remap_source.to_utf8_buffer(), false);
			skip();
		} else {
			add_file(p_path.get_basename() + ".gdc", file, true);
		}
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
