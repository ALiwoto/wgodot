// wgodot-changes::file

#include "modules/gdscript/wgodot_gd/script_resolution.h"

#include "export_analysis.h"

namespace WGodotGDScriptResolution {
using WGodotGDScriptExportTransform::ExportAnalysis;

bool is_global_class(const StringName &p_name) {
	auto *active = ExportAnalysis::get_active();
	return active != nullptr ? active->is_global_class(p_name) : ScriptServer::is_global_class(p_name);
}

String get_global_class_path(const StringName &p_name) {
	auto *active = ExportAnalysis::get_active();
	if (active == nullptr) {
		return ScriptServer::get_global_class_path(p_name);
	}
	return active->get_global_class_path(p_name);
}

StringName get_global_class_native_base(const StringName &p_name) {
	auto *active = ExportAnalysis::get_active();
	if (active == nullptr) {
		return ScriptServer::get_global_class_native_base(p_name);
	}
	return active->get_global_class_native_base(p_name);
}

Ref<Script> load_script(const String &p_path, const String &p_type) {
	auto *active = ExportAnalysis::get_active();
	if (active != nullptr && active->get_source(p_path) != nullptr) {
		Error error = OK;
		return active->get_shallow_script(p_path, error);
	}
	return ResourceLoader::load(p_path, p_type);
}

bool resource_exists(const String &p_path) {
	auto *active = ExportAnalysis::get_active();
	return (active != nullptr && active->get_source(p_path) != nullptr) || ResourceLoader::exists(p_path);
}

String get_resource_type(const String &p_path) {
	auto *active = ExportAnalysis::get_active();
	return active != nullptr && active->get_source(p_path) != nullptr ? "GDScript" : ResourceLoader::get_resource_type(p_path);
}

bool is_export_analysis() {
	return ExportAnalysis::get_active() != nullptr;
}

bool get_parser_override(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, Ref<GDScriptParserRef> &r_parser) {
	if (auto *analysis = ExportAnalysis::get_active()) {
		r_parser = analysis->get_parser(p_path, p_status, r_error);
		return true;
	}
	return false;
}

bool has_parser_override(const String &p_path, bool &r_exists) {
	if (auto *analysis = ExportAnalysis::get_active()) {
		r_exists = analysis->get_source(p_path) != nullptr;
		return true;
	}
	return false;
}

bool get_shallow_script_override(const String &p_path, Error &r_error, Ref<GDScript> &r_script) {
	if (auto *analysis = ExportAnalysis::get_active()) {
		r_script = analysis->get_shallow_script(p_path, r_error);
		return true;
	}
	return false;
}

Ref<GDScriptParserRef> get_global_class_parser_override(const StringName &p_name) {
	if (auto *analysis = ExportAnalysis::get_active()) {
		const String path = get_global_class_path(p_name);
		if (analysis->get_source(path) != nullptr) {
			Error error = OK;
			Ref<GDScriptParserRef> ref = analysis->get_parser(path, GDScriptParserRef::INHERITANCE_SOLVED, error);
			if (error == OK) {
				return ref;
			}
		}
	}
	return Ref<GDScriptParserRef>();
}

const HashMap<uint64_t, String> *get_string_resources_override() {
	auto *analysis = ExportAnalysis::get_active();
	return analysis != nullptr ? &analysis->get_decoded_string_resources() : nullptr;
}

} // namespace WGodotGDScriptResolution
