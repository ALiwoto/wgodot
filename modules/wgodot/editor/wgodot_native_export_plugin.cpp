// wgodot-changes::file
#include "wgodot_native_export_plugin.h"

#include "wgodot_native_resource_policy.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/wgodot_resource_paths.h"
#include "core/object/class_db.h"
#include "editor/export/editor_export_preset.h"
#include "editor/import/3d/wgodot_mesh_collision.h"

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
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::STRING, "wgodot/native_target", PROPERTY_HINT_ENUM, "client,server"), "client"));
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
	if (!GLOBAL_GET("wgodot/gdscript/strict_signal_callable_checking")) {
		set_export_error(ERR_UNCONFIGURED, "Native export requires strict signal/callable checking. Enable 'wgodot/gdscript/strict_signal_callable_checking' in Project Settings.");
		return;
	}
	const String directory = get_export_preset()->get("wgodot/native_module");
	String target_error;
	const Error target_result = target.load(get_export_preset()->get("wgodot/native_target"), target_error);
	if (target_result != OK) {
		set_export_error(target_result, target_error);
		return;
	}
	manifest = read_manifest(directory.path_join("main_game.json"));
	if (int(manifest.get("format", 0)) != 1 || !manifest.has("native_classes") || !manifest.has("sources") || !manifest.has("resource_paths") || !manifest.has("compiled_resources") || !manifest.has("compiled_resource_aliases") || !manifest.has("resource_sources")) {
		set_export_error(ERR_UNCONFIGURED, "Generate the native module first with wg export-cpp. Invalid manifest in: " + directory);
		return;
	}
	if (manifest.get("target", "") != target.get_name() || manifest.get("target_fingerprint", "") != target.get_fingerprint()) {
		set_export_error(ERR_INVALID_DATA, "Native target or project/target settings changed. Regenerate with --target " + target.get_name() + " and rebuild.");
		return;
	}
	const String template_path = get_export_preset()->get(p_debug ? "custom_template/debug" : "custom_template/release");
	const Dictionary build = read_manifest(template_path + ".native.json");
	if (build.get("target", "") != target.get_name() || build.get("generation", "") != manifest["generation"] || String(build.get("binary_sha256", "")) != FileAccess::get_sha256(template_path)) {
		set_export_error(ERR_UNCONFIGURED, "Native template does not match this generation. Run build_wgodot.ps1 -Game (and -Release for release export). Template: " + template_path);
		return;
	}
	const Dictionary sources = manifest["sources"];
	const Dictionary runtime_libraries = build.get("runtime_libraries", Dictionary());
	for (const KeyValue<Variant, Variant> &library : runtime_libraries) {
		const String name = library.key;
		if (name.is_empty() || name != name.get_file() || name == "." || name == "..") {
			set_export_error(ERR_INVALID_DATA, "Invalid native runtime library name.");
			return;
		}
		const String path = template_path.get_base_dir().path_join(name);
		if (!FileAccess::exists(path) || FileAccess::get_sha256(path) != String(library.value)) {
			set_export_error(ERR_INVALID_DATA, "Native runtime library changed or is missing: " + path);
			return;
		}
		add_shared_object(path, Vector<String>());
	}
	for (const KeyValue<Variant, Variant> &source : sources) {
		if (!FileAccess::exists(source.key) || FileAccess::get_sha256(source.key) != String(source.value)) {
			set_export_error(ERR_INVALID_DATA, "Script changed after native generation; regenerate and rebuild: " + String(source.key));
			return;
		}
	}
	const Dictionary editor_scripts = manifest.get("editor_only_scripts", Dictionary());
	for (const KeyValue<Variant, Variant> &source : editor_scripts) {
		if (!FileAccess::exists(source.key) || FileAccess::get_sha256(source.key) != String(source.value)) {
			set_export_error(ERR_INVALID_DATA, "Editor-only script changed after native generation; regenerate: " + String(source.key));
			return;
		}
		target.exclude_script(source.key);
	}
	resources.initialize(manifest["resource_paths"], &target);
	const Dictionary resource_sources = manifest["resource_sources"];
	for (const KeyValue<Variant, Variant> &source : resource_sources) {
		if (!FileAccess::exists(source.key) || FileAccess::get_sha256(source.key) != String(source.value)) {
			set_export_error(ERR_INVALID_DATA, "Resource changed after native generation; regenerate and rebuild: " + String(source.key));
			return;
		}
	}
	const Dictionary compiled = manifest["compiled_resources"];
	for (const KeyValue<Variant, Variant> &entry : compiled) {
		resources.add_native_resource(entry.key, entry.value);
	}
	const Dictionary aliases = manifest["compiled_resource_aliases"];
	for (const KeyValue<Variant, Variant> &alias : aliases) {
		resources.add_native_resource(alias.key, compiled[alias.value], alias.value);
	}
	validated = true;
}

void WGodotNativeExportPlugin::_export_paths_ready(const HashSet<String> &p_paths) {
	if (!validated) {
		return;
	}
	const Dictionary sources = manifest["sources"];
	const Dictionary compiled = manifest["compiled_resources"];
	const Dictionary aliases = manifest["compiled_resource_aliases"];
	// Imported payloads live under .godot rather than beside their source. Keep
	// ownership tied to that source, including alternate platform payloads.
	for (const String &path : p_paths) {
		if (!FileAccess::exists(path + ".import")) {
			continue;
		}
		String message;
		const Error error = target.read_import(path, message);
		if (error != OK) {
			set_export_error(error, message);
			return;
		}
	}
	for (const String &path : p_paths) {
		if (!target.includes(path)) {
			continue;
		}
		if (path.get_extension() == "gd" && !sources.has(path)) {
			set_export_error(ERR_INVALID_DATA, "Script was added after native generation; regenerate and rebuild: " + path);
			return;
		}
		if (WGodotNativeResourcePolicy::is_authored(path) && !compiled.has(path) && !aliases.has(path)) {
			const Ref<Resource> resource = ResourceLoader::load(path);
			if (resource.is_null()) {
				set_export_error(ERR_CANT_OPEN, "Cannot load native export resource: " + path);
				return;
			}
			if (WGodotNativeResourcePolicy::needs_factory(resource)) {
				set_export_error(ERR_INVALID_DATA, "Resource was added after native generation; regenerate and rebuild: " + path);
				return;
			}
		}
	}
	const Dictionary classes = manifest["native_classes"];
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &entry : ProjectSettings::get_singleton()->get_autoload_list()) {
		if (!target.includes(entry.value.path)) {
			autoloads["autoload/" + String(entry.key)] = Variant();
			continue;
		}
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
		if (!compiled.has(path)) {
			set_export_error(ERR_INVALID_DATA, "Autoload has no compiled scene factory: " + entry.value.path);
			return;
		}
		autoloads["autoload/" + String(entry.key)] = (entry.value.is_singleton ? "*" : "") + path;
	}
}

void WGodotNativeExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (WGodotMeshCollision::is_superseded(p_path)) {
		skip();
		return;
	}
	if (!enabled) {
		return;
	}
	if (!target.includes(p_path)) {
		skip();
		return;
	}
	const String extension = p_path.get_extension().to_lower();
	const Dictionary compiled = manifest.get("compiled_resources", Dictionary());
	const Dictionary aliases = manifest.get("compiled_resource_aliases", Dictionary());
	if (compiled.has(p_path) || aliases.has(p_path)) {
		skip();
		return;
	}
	if (p_type == "GDScript" || p_type == "Script" || extension == "gd" || extension == "gdc" || extension == "gde" || (extension == "uid" && p_path.get_basename().get_extension() == "gd")) {
		skip();
		return;
	}
	if (p_type != "PackedScene" && extension != "scn" && extension != "tscn" && extension != "res" && extension != "tres") {
		return;
	}
	Ref<Resource> resource = ResourceLoader::load(p_path);
	if (resource.is_null()) {
		set_export_error(ERR_CANT_OPEN, "Cannot load external resource: " + p_path);
		return;
	}
	String message;
	const Error error = WGodotNativeResourcePolicy::prepare_external(resource, target, message);
	if (error != OK) {
		set_export_error(error, p_path + ": " + message);
	}
	// The pack writer applies the same scene filter and remaps references while
	// retaining mesh/animation data as binary payloads rather than generated C++.
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
		if (path.get_extension() == "gd" || !target.includes(path)) {
			scripts.push_back(path);
		}
	}
	for (const String &path : scripts) {
		r_paths.erase(path);
	}
}

void WGodotNativeExportPlugin::_export_project_settings(HashMap<String, Variant> &r_settings) {
	if (!validated) {
		return;
	}
	for (const KeyValue<Variant, Variant> &entry : autoloads) {
		r_settings[entry.key] = entry.value;
	}
	for (const KeyValue<String, Variant> &entry : target.get_settings()) {
		r_settings[entry.key] = entry.value;
	}
	List<PropertyInfo> properties;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	settings->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		const Variant original = r_settings.has(property.name) ? r_settings[property.name] : settings->get(property.name);
		if (!r_settings.has(property.name) && original.get_type() == Variant::STRING && original == settings->property_get_revert(property.name)) {
			const String path = original;
			// Defaults may name optional resources, such as the default audio bus layout.
			// Keep absent defaults unchanged; explicitly configured missing assets still fail.
			if ((path.begins_with("res://") || path.begins_with("uid://")) && !ResourceLoader::exists(path)) {
				continue;
			}
		}
		const Variant rewritten = resources.rewrite_value(original, property);
		if (rewritten != original) {
			r_settings[property.name] = rewritten;
		}
	}
}

Error WGodotNativeExportPlugin::_export_pack_file(String &r_path, Vector<uint8_t> &r_data, const String &p_source_path) {
	if (!validated) {
		return OK;
	}
	if (r_path != p_source_path) {
		// Godot's generated payload retains the ownership of its authored source.
		target.add_import(r_path, p_source_path);
	}
	return resources.export_file(r_path, r_data);
}

Error WGodotNativeExportPlugin::_export_pack_finish(HashMap<String, Vector<uint8_t>> &r_files) {
	if (!validated) {
		return OK;
	}
	Vector<uint8_t> catalog;
	const Error error = resources.finish(catalog);
	if (error != OK) {
		set_export_error(error, "Cannot finalize the native resource catalog.");
		return error;
	}
	r_files.insert(WGodotResourcePaths::CATALOG_PATH, catalog);
	return OK;
}
