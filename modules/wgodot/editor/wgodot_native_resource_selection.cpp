// wgodot-changes::file
#include "wgodot_native_resource_selection.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

Error WGodotNativeResourceSelection::load(const String &p_target, String &r_error) {
	all_resources = true;
	roots.clear();
	fingerprint = String().sha256_text();
	r_error = String();
	const String config_path = "res://export_presets.cfg";
	if (!FileAccess::exists(config_path)) {
		return OK;
	}
	Ref<ConfigFile> config;
	config.instantiate();
	const Error error = config->load(config_path);
	if (error != OK) {
		r_error = "Cannot load native resource selection from " + config_path + ": " + error_names[error];
		return error;
	}
	bool matched = false;
	Vector<String> identities;
	for (const String &section : config->get_sections()) {
		if (!section.begins_with("preset.") || section.ends_with(".options")) {
			continue;
		}
		const String features = config->get_value(section, "custom_features", "");
		bool native = false;
		for (const String &feature : features.split(",", false)) {
			if (feature.strip_edges() == "wgodot_native") {
				native = true;
				break;
			}
		}
		if (!native || String(config->get_value(section + ".options", "wgodot/native_target", "client")) != p_target) {
			continue;
		}
		if (!matched) {
			all_resources = false;
			matched = true;
		}
		const String filter = config->get_value(section, "export_filter", "all_resources");
		const PackedStringArray files = config->get_value(section, "export_files", PackedStringArray());
		Ref<ConfigFile> identity;
		identity.instantiate();
		identity->set_value("selection", "export_filter", filter);
		identity->set_value("selection", "export_files", files);
		identity->set_value("selection", "include_filter", config->get_value(section, "include_filter", ""));
		identity->set_value("selection", "exclude_filter", config->get_value(section, "exclude_filter", ""));
		identities.push_back(identity->encode_to_text());
		if (filter != "scenes" && filter != "resources") {
			all_resources = true;
			continue;
		}
		for (const String &path : files) {
			if (filter == "scenes" && ResourceLoader::get_resource_type(path) != "PackedScene") {
				continue;
			}
			roots.insert(path);
		}
	}
	identities.sort();
	String identity;
	for (const String &preset : identities) {
		identity += itos(preset.length()) + ":" + preset;
	}
	fingerprint = identity.sha256_text();
	return OK;
}
