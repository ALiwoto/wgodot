// wgodot-changes::file
#include "export_pipeline.h"
#include "export_maps.h"

#include "editor/export/editor_export_plugin.h"
#include "modules/gdscript/gdscript_tokenizer_buffer.h"
#include "modules/gdscript/wgodot_gd/builtin_class_aliases.h"
#include "modules/gdscript/wgodot_gd/interface_method_aliases.h"
#include "modules/gdscript/wgodot_gd/string_obfuscation.h"

namespace WGodotGDScriptExportTransform {

void ExportPipeline::prepare_export(EditorExportPlugin *p_plugin, const HashSet<String> &p_paths, const TransformOptions &p_options) {
	String details;
	Error error = prepare(p_paths, p_options, details);
	if (error != OK) {
		p_plugin->set_export_error(error, details);
		return;
	}
	const Vector<uint8_t> builtin_class_aliases = WGodotGDScriptBuiltinClassAliases::serialize_alias_map(artifacts);
	if (!builtin_class_aliases.is_empty()) {
		p_plugin->add_file(WGodotGDScriptBuiltinClassAliases::get_alias_map_path(), builtin_class_aliases, false);
	}
	const Vector<uint8_t> interface_method_aliases = WGodotGDScriptInterfaceMethodAliases::serialize_alias_map(artifacts);
	if (!interface_method_aliases.is_empty()) {
		p_plugin->add_file(WGodotGDScriptInterfaceMethodAliases::get_alias_map_path(), interface_method_aliases, false);
	}
	const Vector<uint8_t> string_map = WGodotGDScriptStringObfuscation::serialize_string_map(artifacts);
	if (!string_map.is_empty()) {
		p_plugin->add_file(WGodotGDScriptStringObfuscation::get_string_map_path(), string_map, false);
	}
}

Error ExportPipeline::complete_export(EditorExportPlugin *p_plugin, bool p_redact_diagnostics, const String &p_diagnostic_map_path) {
	if (p_redact_diagnostics) {
		const Error error = get_artifacts().get_diagnostics().save_map(p_diagnostic_map_path);
		if (error != OK) {
			p_plugin->set_export_error(error, "Cannot write diagnostic map: " + p_diagnostic_map_path);
			return error;
		}
	}
	return OK;
}

void ExportPipeline::export_file(EditorExportPlugin *p_plugin, const String &p_path, EditorExportPreset::ScriptExportMode p_script_mode) {
	// Private maps from earlier exports must not enter a later package through
	// resource include filters, even when redaction is currently disabled.
	if (p_path.ends_with(".diagnostics.json")) {
		p_plugin->skip();
		return;
	}
	if (p_path.get_extension() != "gd") {
		return;
	}
	const auto *prepared = get_source(p_path);
	if (prepared == nullptr) {
		p_plugin->set_export_error(ERR_INVALID_DATA, "Missing prepared GDScript export: " + p_path);
		return;
	}
	const String &source = prepared->get_text();
	const String obfuscated_script_path = artifacts.get_exported_script_path(p_path);
	const bool script_path_changed = !obfuscated_script_path.is_empty();
	if (p_script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
		p_plugin->add_file(script_path_changed ? obfuscated_script_path : p_path, source.to_utf8_buffer(), false);
		p_plugin->skip();
		return;
	}
	GDScriptTokenizerBuffer::CompressMode compress_mode = p_script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? GDScriptTokenizerBuffer::COMPRESS_ZSTD : GDScriptTokenizerBuffer::COMPRESS_NONE;
	Vector<uint8_t> file = GDScriptTokenizerBuffer::parse_code_string(source, compress_mode);
	if (file.is_empty()) {
		p_plugin->set_export_error(ERR_PARSE_ERROR, "Cannot tokenize prepared GDScript export: " + p_path);
		return;
	}

	if (script_path_changed) {
		const String obfuscated_binary_script_path = artifacts.get_exported_binary_script_path(p_path);
		const String remap_source = "[remap]\n\npath=\"" + obfuscated_binary_script_path.c_escape() + "\"\n";
		p_plugin->add_file(obfuscated_binary_script_path, file, false);
		p_plugin->add_file(obfuscated_script_path + ".remap", remap_source.to_utf8_buffer(), false);
		p_plugin->skip();
	} else {
		p_plugin->add_file(p_path.get_basename() + ".gdc", file, true);
	}
}

}
