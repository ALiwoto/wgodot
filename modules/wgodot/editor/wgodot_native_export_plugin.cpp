// wgodot-changes::file
#include "wgodot_native_export_plugin.h"
#include "wgodot_native_resource_export.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "editor/file_system/editor_paths.h"
#include "editor/export/editor_export_preset.h"

namespace {
Dictionary read_manifest(const String &p_path) {
	JSON json;
	if (!FileAccess::exists(p_path) || json.parse(FileAccess::get_file_as_string(p_path)) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return json.get_data();
}
} // namespace

void WGodotNativeExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::STRING, "wgodot/native_module", PROPERTY_HINT_GLOBAL_DIR), ""));
}

void WGodotNativeExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	enabled = p_features.has("wgodot_native");
	validated = false;
	manifest.clear();
	autoloads.clear();
	if (!enabled) {
		return;
	}
	if (!GLOBAL_GET("wgodot/gdscript/strict_type_checking")) {
		set_export_error(ERR_UNCONFIGURED, "Native export requires strict type checking. Enable 'wgodot/gdscript/strict_type_checking' in Project Settings.");
		return;
	}
	const String directory = get_export_preset()->get("wgodot/native_module");
	manifest = read_manifest(directory.path_join("main_game.json"));
	if (int(manifest.get("format", 0)) != 1 || !manifest.has("native_classes") || !manifest.has("sources")) {
		set_export_error(ERR_UNCONFIGURED, "Generate the native module first with wg export-cpp. Invalid manifest in: " + directory);
		return;
	}
	const String template_path = get_export_preset()->get(p_debug ? "custom_template/debug" : "custom_template/release");
	const Dictionary build = read_manifest(template_path + ".native.json");
	if (build.get("generation", "") != manifest["generation"] || String(build.get("binary_sha256", "")) != FileAccess::get_sha256(template_path)) {
		set_export_error(ERR_UNCONFIGURED, "Native template does not match this generation. Run build_wgodot.ps1 -Game (and -Release for release export). Template: " + template_path);
		return;
	}
	const Dictionary sources = manifest["sources"];
	for (const KeyValue<Variant, Variant> &source : sources) {
		if (!FileAccess::exists(source.key) || FileAccess::get_sha256(source.key) != String(source.value)) {
			set_export_error(ERR_INVALID_DATA, "Script changed after native generation; regenerate and rebuild: " + String(source.key));
			return;
		}
	}
	validated = true;
}

void WGodotNativeExportPlugin::_export_paths_ready(const HashSet<String> &p_paths) {
	if (!validated) {
		return;
	}
	const Dictionary sources = manifest["sources"];
	for (const String &path : p_paths) {
		if (path.get_extension() == "gd" && !sources.has(path)) {
			set_export_error(ERR_INVALID_DATA, "Script was added after native generation; regenerate and rebuild: " + path);
			return;
		}
	}
	const Dictionary classes = manifest["native_classes"];
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &entry : ProjectSettings::get_singleton()->get_autoload_list()) {
		if (entry.value.path.get_extension() != "gd") {
			continue;
		}
		if (!classes.has(entry.value.path)) {
			set_export_error(ERR_INVALID_DATA, "Autoload has no generated native class: " + entry.value.path);
			return;
		}
		const Dictionary native_class = classes[entry.value.path];
		if (!ClassDB::is_parent_class(StringName(native_class["base"]), SNAME("Node"))) {
			set_export_error(ERR_INVALID_DATA, "A native autoload must extend Node: " + entry.value.path);
			return;
		}
		const String path = "res://.wgodot/native/autoload_" + String(entry.key).sha256_text().substr(0, 16) + ".tscn";
		const String text = "[gd_scene format=3]\n\n[node name=\"" + String(entry.key).c_escape() + "\" type=\"" + String(native_class["class"]) + "\"]\n";
		add_file(path, text.to_utf8_buffer(), false);
		autoloads["autoload/" + String(entry.key)] = (entry.value.is_singleton ? "*" : "") + path;
	}
}

void WGodotNativeExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (!enabled) {
		return;
	}
	const String extension = p_path.get_extension().to_lower();
	if (p_type == "GDScript" || p_type == "Script" || extension == "gd" || extension == "gdc" || extension == "gde" || (extension == "uid" && p_path.get_basename().get_extension() == "gd")) {
		skip();
		return;
	}
	const bool binary = extension == "scn" || extension == "res";
	if (!binary && extension != "tscn" && extension != "tres") {
		return;
	}
	String source_path = p_path;
	String output_path = p_path;
	if (binary) {
		const Ref<Resource> resource = ResourceLoader::load(p_path);
		const String text_extension = p_type == "PackedScene" ? ".tscn" : ".tres";
		source_path = EditorPaths::get_singleton()->get_temp_dir().path_join("wgodot_native_" + p_path.sha256_text() + text_extension);
		if (resource.is_null() || ResourceSaver::save(resource, source_path) != OK) {
			set_export_error(ERR_CANT_CREATE, "Cannot serialize native export resource: " + p_path);
			return;
		}
		output_path = p_path + ".native" + text_extension;
	}
	String output;
	String message;
	const Error error = wgodot_export_native_resource(FileAccess::get_file_as_string(source_path), manifest["native_classes"], output, message);
	if (binary) {
		DirAccess::remove_absolute(source_path);
	}
	if (error != OK) {
		set_export_error(error, p_path + ": " + message);
		return;
	}
	add_file(output_path, output.to_utf8_buffer(), binary);
	skip();
}

void WGodotNativeExportPlugin::_export_global_class_list(Array &r_classes) {
	if (enabled) {
		r_classes.clear();
	}
}

void WGodotNativeExportPlugin::_export_cache_paths(HashSet<String> &r_paths) {
	if (!enabled) {
		return;
	}
	Vector<String> scripts;
	for (const String &path : r_paths) {
		if (path.get_extension() == "gd") {
			scripts.push_back(path);
		}
	}
	for (const String &path : scripts) {
		r_paths.erase(path);
	}
}

void WGodotNativeExportPlugin::_export_project_settings(HashMap<String, Variant> &r_settings) {
	if (enabled) {
		for (const KeyValue<Variant, Variant> &entry : autoloads) {
			r_settings[entry.key] = entry.value;
		}
	}
}
