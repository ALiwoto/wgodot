// wgodot-changes::file
#include "wgodot_export_target.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"

bool WGodotExportTarget::valid_owner(const String &p_owner) {
	return p_owner == "shared" || p_owner == "client" || p_owner == "server";
}

bool WGodotExportTarget::includes_owner(const String &p_owner) const {
	return p_owner == "shared" || p_owner == target;
}

Error WGodotExportTarget::load(const String &p_target, String &r_error) {
	target = p_target;
	owners.clear();
	settings.clear();
	imported_sources.clear();
	excluded_scripts.clear();
	if (target != "client" && target != "server") {
		r_error = "Native export target must be client or server.";
		return ERR_INVALID_PARAMETER;
	}
	const bool exists = FileAccess::exists(CONFIG_PATH);
	// Include project settings: entry scenes, autoloads and feature overrides must
	// not change between generation and packaging without a new build identity.
	const String project_identity = target + "\n" + FileAccess::get_sha256("res://project.godot");
	fingerprint = project_identity.sha256_text();
	if (!exists) {
		if (target == "server") {
			r_error = "Server export requires res://wgodot_targets.cfg with explicit ownership and server settings.";
			return ERR_UNCONFIGURED;
		}
		return OK;
	}
	Ref<ConfigFile> config;
	config.instantiate();
	Ref<ConfigFile> active_config;
	active_config.instantiate();
	Error error = config->load(CONFIG_PATH);
	if (error != OK) {
		r_error = "Cannot read " + String(CONFIG_PATH);
		return error;
	}
	for (const String &section : config->get_sections()) {
		if (section != "paths" && section != "client" && section != "server") {
			r_error = "Unknown native target section: " + section;
			return ERR_INVALID_DATA;
		}
		for (const String &key : config->get_section_keys(section)) {
			const Variant value = config->get_value(section, key);
			if (section == "paths" || section == target) {
				active_config->set_value(section, key, value);
			}
			if (section == "paths") {
				const String path = key == "res://" ? key : key.trim_suffix("/");
				if (!key.begins_with("res://") || path.simplify_path() != path || value.get_type() != Variant::STRING || !valid_owner(value)) {
					r_error = "Invalid target ownership: " + key + ". Use canonical res:// paths (directory rules end in /) and shared, client or server.";
					return ERR_INVALID_DATA;
				}
				owners.push_back({ key.ends_with("/") ? key + "*" : key, value });
			} else {
				if (!ProjectSettings::get_singleton()->has_setting(key) || key.begins_with("autoload/")) {
					r_error = "Unknown or unsupported target setting: " + key + ". Autoloads use path ownership.";
					return ERR_INVALID_DATA;
				}
				if (section == target) {
					settings.insert(key, value);
				}
			}
		}
	}
	// A server setting must not invalidate an otherwise identical client build.
	// Ownership rule order remains significant because the last match wins.
	fingerprint = (project_identity + "\n" + active_config->encode_to_text()).sha256_text();
	return OK;
}

bool WGodotExportTarget::includes(const String &p_path) const {
	String path = ResourceUID::ensure_path(p_path).get_slice("::", 0);
	if (path.ends_with(".uid") || path.ends_with(".import") || path.ends_with(".remap")) {
		path = path.get_basename();
	}
	if (const String *source = imported_sources.getptr(path)) {
		path = *source;
	}
	if (path == CONFIG_PATH || excluded_scripts.has(path)) {
		return false;
	}
	const String *owner = nullptr;
	for (const Rule &rule : owners) {
		if (path.match(rule.pattern)) {
			owner = &rule.owner;
		}
	}
	return owner == nullptr || includes_owner(*owner);
}

String WGodotExportTarget::dependency_error(const String &p_path) const {
	if (excluded_scripts.has(ResourceUID::ensure_path(p_path))) {
		return "Runtime content cannot reference @editor_only script: " + p_path;
	}
	return "Forbidden " + target + " dependency: " + p_path + ". Move the dependency to shared content or its consumer to the owning target.";
}

Error WGodotExportTarget::read_import(const String &p_source, String &r_error) {
	Ref<ConfigFile> import;
	import.instantiate();
	const Error error = import->load(p_source + ".import");
	if (error != OK) {
		r_error = "Cannot read import ownership: " + p_source;
		return error;
	}
	const PackedStringArray destinations = import->get_value("deps", "dest_files", PackedStringArray());
	for (const String &destination : destinations) {
		add_import(destination, p_source);
	}
	return OK;
}

void WGodotExportTarget::add_import(const String &p_path, const String &p_source) {
	imported_sources.insert(p_path, p_source);
}
