// wgodot-changes::file
#include "wgodot_resource_export.h"
#include "wgodot_resource_rewrite.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/io/stream_peer.h"
#include "core/io/wgodot_resource_paths.h"
#include "core/io/wgodot_resource_serialization.h"
#include "core/variant/variant_parser.h"
#include "editor/file_system/editor_paths.h"

void WGodotResourceExport::initialize(const Dictionary &p_paths) {
	ids.clear();
	entries.clear();
	payloads.clear();
	native_resources.clear();
	required.clear();
	formats.clear();
	next_id = 1;
	error = OK;
	for (const KeyValue<Variant, Variant> &entry : p_paths) {
		const int64_t id = entry.value;
		ids.insert(entry.key, id);
		if (String(entry.key).get_extension() != "gd") {
			required.insert(id);
		}
		next_id = MAX(next_id, id + 1);
	}
}

void WGodotResourceExport::add_native_resource(const String &p_path, const String &p_type, const String &p_target) {
	const int64_t id = identify(p_path);
	Entry entry;
	entry.format = String("native").hash();
	entry.type = p_type;
	if (!p_target.is_empty()) {
		entry.target = identify(p_target);
	}
	entries.insert(id, entry);
	native_resources.insert(id);
}

int64_t WGodotResourceExport::identify(const String &p_path) {
	String source = ResourceUID::ensure_path(p_path);
	if (source.is_relative_path()) {
		source = "res://" + source;
	}
	source = ProjectSettings::get_singleton()->localize_path(source).simplify_path();
	if (!source.begins_with("res://")) {
		error = ERR_INVALID_DATA;
		ERR_PRINT("Cannot export a resource outside the project: " + p_path);
		return 0;
	}
	if (const int64_t *id = ids.getptr(source)) {
		return *id;
	}
	const int64_t id = next_id++;
	ids.insert(source, id);
	return id;
}

String WGodotResourceExport::path(const String &p_original) {
	const int64_t id = identify(p_original);
	required.insert(id);
	return WGodotResourcePaths::to_path(id);
}

Variant WGodotResourceExport::rewrite_value(const Variant &p_value, const PropertyInfo &p_property) {
	return WGodotResourceRewrite::value(p_value, p_property, [this](const String &p_original) { return path(p_original); });
}

String WGodotResourceExport::rewrite_text(const String &p_text, const String &p_source) {
	return WGodotResourceRewrite::text(p_text, p_source, [this](const String &p_original) { return path(p_original); });
}

Error WGodotResourceExport::import_remap(const String &p_source, const Vector<uint8_t> &p_data) {
	Ref<ConfigFile> config;
	config.instantiate();
	RETURN_IF_ERROR(config->parse(String::utf8(reinterpret_cast<const char *>(p_data.ptr()), p_data.size())));
	const String source = p_source.get_basename();
	const int64_t id = identify(source);
	if (native_resources.has(id)) {
		return OK;
	}
	Entry entry;
	entry.format = source.get_extension().to_lower().hash();
	entry.type = config->get_value("remap", "type", ResourceLoader::get_resource_type(source));
	for (const String &key : config->get_section_keys("remap")) {
		if (key == "path") {
			entry.target = identify(config->get_value("remap", key));
		} else if (key.begins_with("path.")) {
			entry.variants.insert(key.substr(5), identify(config->get_value("remap", key)));
		}
	}
	if (entry.target == 0 && entry.variants.is_empty()) {
		// Some importers only produce separate resources (for example CSV
		// translation files); their source is not itself a loadable resource.
		return error;
	}
	entries.insert(id, entry);
	return error;
}

Error WGodotResourceExport::rewrite_binary(const String &p_source, Vector<uint8_t> &r_data) {
	// Re-serialize through Godot, transforming serialized strings only. This
	// covers dependency tables and properties without changing editor resources.
	const String temporary = EditorPaths::get_singleton()->get_temp_dir().path_join("wgodot_ids_" + p_source.sha256_text() + ".res");
	{
		Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
		ERR_FAIL_COND_V(file.is_null(), ERR_CANT_CREATE);
		file->store_buffer(r_data.ptr(), r_data.size());
	}
	Error result = OK;
	Ref<ResourceFormatLoaderBinary> loader;
	loader.instantiate();
	const Ref<Resource> resource = loader->load(temporary, p_source, &result, false, nullptr, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	if (resource.is_null() && result == OK) {
		result = ERR_FILE_CORRUPT;
	}
	if (resource.is_valid() && result == OK) {
		struct Context {
			WGodotResourceExport *exporter;
			String source;
		} context{ this, p_source };
		WGodotResourceSerialization rewrite([](void *p_context, const String &p_value) {
			Context *context = static_cast<Context *>(p_context);
			if (p_value.contains("#include")) {
				return context->exporter->rewrite_text(p_value, context->source);
			}
			return String(context->exporter->rewrite_value(p_value));
		}, &context);
		ResourceFormatSaverBinaryInstance saver;
		const uint32_t flags = memcmp(r_data.ptr(), "RSCC", 4) == 0 ? ResourceSaver::FLAG_COMPRESS : ResourceSaver::FLAG_NONE;
		result = saver.save(temporary, resource, flags);
	}
	if (result == OK) {
		r_data = FileAccess::get_file_as_bytes(temporary);
	}
	DirAccess::remove_absolute(temporary);
	return result;
}

Error WGodotResourceExport::export_file(String &r_path, Vector<uint8_t> &r_data) {
	const String source = r_path.begins_with("res://") ? r_path : "res://" + r_path;
	if (source == "res://project.binary") {
		return OK; // Bootstrap settings are rewritten before binary serialization.
	}
	if (source == ProjectSettings::get_singleton()->get_global_class_list_path()) {
		// Native export clears script classes. Keep the engine's bootstrap file
		// at its fixed location so startup does not search for a missing cache.
		return OK;
	}
	if (source == ResourceUID::get_cache_file()) {
		Ref<StreamPeerBuffer> input;
		input.instantiate();
		input->set_data_array(r_data);
		Vector<Pair<ResourceUID::ID, String>> records;
		const uint32_t count = input->get_u32();
		for (uint32_t i = 0; i < count; i++) {
			const ResourceUID::ID uid = input->get_u64();
			const String original = input->get_utf8_string();
			const int64_t id = identify(original);
			if (entries.has(id)) {
				records.push_back({ uid, WGodotResourcePaths::to_path(id) });
			}
		}
		r_data = ResourceUID::encode_binary_cache(records);
		return error;
	}
	if (source.ends_with(".import") || source.ends_with(".remap")) {
		RETURN_IF_ERROR(import_remap(source, r_data));
		r_path = String();
		return OK;
	}
	const int64_t id = identify(source);
	if (native_resources.has(id)) {
		r_path = String();
		return OK;
	}
	Entry entry = entries.has(id) ? entries[id] : Entry();
	const String extension = source.get_extension().to_lower();
	entry.format = extension.hash();
	if (FileAccess::exists(source)) {
		entry.type = ResourceLoader::get_resource_type(source);
	}
	if (const String *existing = formats.getptr(entry.format)) {
		ERR_FAIL_COND_V_MSG(*existing != extension, ERR_ALREADY_EXISTS, "Native resource format identifier collision.");
	}
	formats.insert(entry.format, extension);
	if (extension == "tscn" || extension == "tres" || extension == "gdshader" || extension == "gdshaderinc") {
		const String text = String::utf8(reinterpret_cast<const char *>(r_data.ptr()), r_data.size());
		if (extension == "tscn" || extension == "tres") {
			VariantParser::StreamString stream;
			stream.s = text;
			VariantParser::Tag header;
			int line = 1;
			String message;
			RETURN_IF_ERROR(VariantParser::parse_tag(&stream, line, message, header));
			entry.type = header.name == "gd_scene" ? String("PackedScene") : String(header.fields["type"]);
		}
		r_data = rewrite_text(text, source).to_utf8_buffer();
	} else if (r_data.size() >= 4 && (memcmp(r_data.ptr(), "RSRC", 4) == 0 || memcmp(r_data.ptr(), "RSCC", 4) == 0)) {
		RETURN_IF_ERROR(rewrite_binary(source, r_data));
	}
	entries.insert(id, entry);
	payloads.insert(id);
	r_path = WGodotResourcePaths::to_path(id);
	return error;
}

Error WGodotResourceExport::finish(Vector<uint8_t> &r_catalog) {
	RETURN_IF_ERROR(error);
	for (const KeyValue<String, int64_t> &resource : ids) {
		ERR_FAIL_COND_V_MSG(required.has(resource.value) && !entries.has(resource.value), ERR_FILE_MISSING_DEPENDENCIES,
				"Resource required by native export is excluded: " + resource.key + " (ID " + itos(resource.value) + ")");
	}
	HashSet<int64_t> visiting;
	HashSet<int64_t> validated;
	auto validate = [&](auto &&p_self, int64_t p_id) -> Error {
		if (validated.has(p_id)) {
			return OK;
		}
		ERR_FAIL_COND_V_MSG(visiting.has(p_id), ERR_CYCLIC_LINK, "Cyclic native resource remap: " + itos(p_id));
		const Entry *entry = entries.getptr(p_id);
		ERR_FAIL_NULL_V_MSG(entry, ERR_FILE_MISSING_DEPENDENCIES, "Native resource payload is absent: " + itos(p_id));
		visiting.insert(p_id);
		if (entry->target != 0) {
			RETURN_IF_ERROR(p_self(p_self, entry->target));
		} else if (entry->variants.is_empty()) {
			ERR_FAIL_COND_V_MSG(!payloads.has(p_id) && !native_resources.has(p_id), ERR_FILE_MISSING_DEPENDENCIES, "Native resource has no payload: " + itos(p_id));
		}
		for (const KeyValue<String, int64_t> &variant : entry->variants) {
			RETURN_IF_ERROR(p_self(p_self, variant.value));
		}
		visiting.erase(p_id);
		validated.insert(p_id);
		return OK;
	};
	for (const KeyValue<int64_t, Entry> &entry : entries) {
		RETURN_IF_ERROR(validate(validate, entry.key));
	}
	Ref<StreamPeerBuffer> buffer;
	buffer.instantiate();
	buffer->put_u32(WGodotResourcePaths::CATALOG_MAGIC);
	buffer->put_u32(entries.size());
	Vector<int64_t> ordered;
	for (const KeyValue<int64_t, Entry> &entry : entries) {
		ordered.push_back(entry.key);
	}
	ordered.sort();
	for (const int64_t id : ordered) {
		const Entry &entry = entries[id];
		buffer->put_u64(id);
		buffer->put_u64(entry.target);
		buffer->put_u32(entry.format);
		buffer->put_utf8_string(entry.type);
		buffer->put_u32(entry.variants.size());
		for (const KeyValue<String, int64_t> &variant : entry.variants) {
			buffer->put_utf8_string(variant.key);
			buffer->put_u64(variant.value);
		}
	}
	r_catalog = buffer->get_data_array();
	return OK;
}
