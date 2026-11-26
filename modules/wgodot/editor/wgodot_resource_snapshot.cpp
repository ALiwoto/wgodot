// wgodot-changes::file
#include "wgodot_resource_snapshot.h"

#include "wgodot_resource_schema.h"

#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/io/wgodot_file_stamp.h"

const WGodotResourceSnapshot::Field *WGodotResourceSnapshot::Schema::field(const StringName &p_name) const {
	for (const Field &entry : fields) {
		if (entry.name == p_name) {
			return &entry;
		}
	}
	return nullptr;
}

Dictionary WGodotResourceSnapshot::capture_class(const GDScriptParser::ClassNode *p_class, const String &p_script, const String &p_cpp_name) {
	const WGodotResourceSchema schema(p_class);
	Dictionary result;
	result["id"] = int64_t(schema.id);
	result["fingerprint"] = int64_t(schema.fingerprint);
	result["cpp_name"] = p_cpp_name;
	result["script"] = p_script;
	result["concrete"] = !p_class->is_abstract && !p_class->wgodot_is_interface && !schema.fields.is_empty();
	PackedStringArray ancestors;
	for (const auto *node = p_class; node; node = node->base_type.kind == GDScriptParser::DataType::CLASS ? node->base_type.class_type : nullptr) {
		ancestors.push_back(node->fqcn);
	}
	result["ancestors"] = ancestors;
	Array fields;
	for (const auto *field : schema.fields) {
		Dictionary entry;
		entry["name"] = field->identifier->name;
		entry["type"] = WGodotResourceEncode::Type(field->type_constraint).serialize();
		fields.push_back(entry);
	}
	result["fields"] = fields;
	return result;
}

Dictionary WGodotResourceSnapshot::settings_revisions() {
	Dictionary result;
	for (const char *path : { "res://project.godot", "res://override.cfg", "res://wgodot_targets.cfg", "res://export_presets.cfg" }) {
		result[path] = WGodotFileStamp::get(path);
	}
	return result;
}

Error WGodotResourceSnapshot::load(const String &p_path) {
	Error error;
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
	RETURN_IF_ERROR(error);
	const Variant value = file->get_var(false);
	ERR_FAIL_COND_V(value.get_type() != Variant::DICTIONARY, ERR_FILE_CORRUPT);
	const Dictionary data = value;
	ERR_FAIL_COND_V(int(data.get("version", 0)) != VERSION, ERR_FILE_UNRECOGNIZED);
	settings = data["settings"];
	scripts = data["scripts"];
	const Dictionary schemas = data["classes"];
	for (const auto &item : schemas) {
		const Dictionary entry = item.value;
		Schema schema;
		schema.id = int64_t(entry["id"]);
		schema.fingerprint = int64_t(entry["fingerprint"]);
		schema.cpp_name = entry["cpp_name"];
		schema.script = entry["script"];
		schema.concrete = entry["concrete"];
		schema.ancestors = entry["ancestors"];
		const Array fields = entry["fields"];
		for (const Variant &field : fields) {
			const Dictionary declaration = field;
			schema.fields.push_back({ declaration["name"], WGodotResourceEncode::Type(Dictionary(declaration["type"])) });
		}
		classes.insert(item.key, schema);
	}
	return OK;
}

bool WGodotResourceSnapshot::derives(const StringName &p_class, const StringName &p_base) const {
	if (p_class == p_base || p_base == SNAME("Resource")) {
		return true;
	}
	const Schema *schema = classes.getptr(p_class);
	return schema && schema->ancestors.has(p_base);
}

PackedByteArray WGodotResourceSnapshot::pack_identity(const Dictionary &p_manifest) {
	Dictionary identity;
	identity["generation"] = p_manifest["generation"];
	identity["resources"] = p_manifest.get("resource_generation", "");
	int size = 0;
	encode_variant(identity, nullptr, size, false);
	PackedByteArray bytes;
	bytes.resize(size);
	encode_variant(identity, bytes.ptrw(), size, false);
	return bytes;
}
