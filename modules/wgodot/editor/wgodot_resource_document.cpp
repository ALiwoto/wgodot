// wgodot-changes::file
#include "wgodot_resource_document.h"

#include "core/io/wgodot_file_stamp.h"
#include "scene/resources/wgodot_text_resource_cache.h"

using VP = VariantParser;

Error WGodotResourceDocument::declare(const StringName &p_name) {
	const auto *schema = snapshot.classes.getptr(p_name);
	ERR_FAIL_NULL_V(schema, ERR_UNAVAILABLE);
	for (const String &ancestor : schema->ancestors) {
		const auto *base = snapshot.classes.getptr(ancestor);
		if (base && snapshot.scripts.has(base->script)) {
			scripts[base->script] = snapshot.scripts[base->script];
		}
	}
	return OK;
}

Error WGodotResourceDocument::field(const String &p_name, WGodotResourceDataType &r_type, bool &r_found, String &r_error) {
	r_found = false;
	if (current.is_null()) {
		r_error = "Assignment outside a resource section.";
		return ERR_PARSE_ERROR;
	}
	const auto *schema = snapshot.classes.getptr(current->get_original_class());
	const auto *declaration = schema ? schema->field(p_name) : nullptr;
	if (!declaration) {
		r_error = "Field is not in the compiled export schema: " + current->get_original_class() + "." + p_name;
		return ERR_INVALID_DATA;
	}
	// WRecord needs its separately registered value schema. Never execute a
	// script or silently erase that schema in this resource-only reader.
	Vector<const WGodotResourceEncode::Type *> types;
	types.push_back(&declaration->type);
	for (int i = 0; i < types.size(); i++) {
		const auto &type = *types[i];
		if (type.builtin_type == Variant::WRECORD || type.wgodot_class_ref) {
			r_error = "Record/ClassRef fields require normal native generation: " + p_name;
			return ERR_UNAVAILABLE;
		}
		for (const auto &element : type.elements) {
			types.push_back(&element);
		}
	}
	r_type = declaration->type.declaration();
	r_found = true;
	return OK;
}

Error WGodotResourceDocument::validate(const Variant &p_value, const WGodotResourceEncode::Type &p_type, String &r_error) {
	const Variant::Type actual = p_value.get_type();
	if (actual != p_type.builtin_type && !(actual == Variant::NIL && p_type.builtin_type == Variant::OBJECT) &&
			!(actual == Variant::INT && p_type.builtin_type == Variant::FLOAT) && !(actual == Variant::STRING && p_type.builtin_type == Variant::STRING_NAME)) {
		r_error = "Expected " + Variant::get_type_name(p_type.builtin_type) + ", got " + Variant::get_type_name(actual);
		return ERR_INVALID_DATA;
	}
	if (p_type.builtin_type == Variant::OBJECT) {
		const Ref<MissingResource> value = p_value;
		if (!p_value.is_null() && value.is_null()) {
			r_error = "Only declared resource references are accepted in a data-only update.";
			return ERR_INVALID_DATA;
		}
		if (value.is_valid() && !snapshot.derives(value->get_original_class(), p_type.name)) {
			r_error = "Expected resource " + String(p_type.name) + ", got " + value->get_original_class();
			return ERR_INVALID_DATA;
		}
	} else if (p_type.wgodot_resource_path) {
		if (!String(p_value).is_empty() && !resource_paths.has(p_value)) {
			r_error = "Resource path was not compiled; run normal generation: " + String(p_value);
			return ERR_INVALID_DATA;
		}
	} else if (p_type.builtin_type == Variant::ARRAY) {
		const Array array = p_value;
		for (const Variant &value : array) {
			RETURN_IF_ERROR(validate(value, p_type.elements[0], r_error));
		}
	} else if (p_type.builtin_type == Variant::DICTIONARY) {
		const Dictionary map = p_value;
		for (const auto &entry : map) {
			RETURN_IF_ERROR(validate(entry.key, p_type.elements[0], r_error));
			RETURN_IF_ERROR(validate(entry.value, p_type.elements[1], r_error));
		}
	}
	return OK;
}

Error WGodotResourceDocument::reference(void *p_self, VP::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error, bool p_external) {
	auto &self = *static_cast<WGodotResourceDocument *>(p_self);
	VP::Token token;
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	if (token.type != VP::TK_STRING) {
		r_error = "Expected resource reference ID.";
		return ERR_PARSE_ERROR;
	}
	const String id = token.value;
	const auto &table = p_external ? self.external : self.internal;
	if (!table.has(id)) {
		r_error = "Unknown resource reference: " + id;
		return ERR_INVALID_DATA;
	}
	r_resource = table[id];
	RETURN_IF_ERROR(VP::get_token(p_stream, token, r_line, r_error));
	return token.type == VP::TK_PARENTHESIS_CLOSE ? OK : ERR_PARSE_ERROR;
}

Error WGodotResourceDocument::external_reference(void *p_self, VP::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error) {
	return reference(p_self, p_stream, r_resource, r_line, r_error, true);
}

Error WGodotResourceDocument::internal_reference(void *p_self, VP::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error) {
	return reference(p_self, p_stream, r_resource, r_line, r_error, false);
}

Error WGodotResourceDocument::direct_reference(void *, VP::Stream *, Ref<Resource> &, int &, String &r_error) {
	r_error = "Inline Resource() references require normal generation; use an authored ExtResource.";
	return ERR_UNAVAILABLE;
}

Error WGodotResourceDocument::parse(String &r_error, int &r_line) {
	Error error;
	VP::StreamFile stream;
	stream.f = FileAccess::open(source, FileAccess::READ, &error);
	RETURN_IF_ERROR(error);
	VP::ResourceParser references;
	references.userdata = this;
	references.ext_func = external_reference;
	references.sub_func = internal_reference;
	references.func = direct_reference;
	references.skip_class_loading = true;
	references.syntax_only = true;
	references.require_resolved_values = true;
	VP::Tag tag;
	RETURN_IF_ERROR(VP::parse_tag(&stream, r_line, r_error, tag, &references));
	if (tag.name != "gd_resource" || !tag.fields.has("type")) {
		r_error = "Data-only updates require a named resource type.";
		return ERR_UNAVAILABLE;
	}
	const StringName type = tag.fields["type"];
	const auto *root_schema = snapshot.classes.getptr(type);
	if (!root_schema || !root_schema->concrete || root_schema->cpp_name != String(compiled_resources.get(source, ""))) {
		r_error = "Resource root class changed or was not compiled; run normal generation.";
		return ERR_INVALID_DATA;
	}
	root.instantiate();
	root->set_original_class(type);
	root->set_path_cache(source);
	root->set_recording_properties(true);
	graph.push_back(root);
	identities.insert(root.ptr(), 1);
	properties.insert(root, {});
	RETURN_IF_ERROR(declare(type));
	WGodotNamedTypes::Resolver resolver;
	WGodotResourceValueParser values(source, StringName(), Ref<Script>(), resolver);
	values.set_field_resolver(this);
	bool root_section = false;
	while (true) {
		String assignment;
		Variant value;
		tag = VP::Tag();
		error = VP::parse_tag_assign_eof(&stream, r_line, r_error, tag, assignment, value, &references, false, &values);
		if (error == ERR_FILE_EOF) {
			return root_section ? OK : ERR_PARSE_ERROR;
		}
		RETURN_IF_ERROR(error);
		if (!assignment.is_empty()) {
			const auto &schema = snapshot.classes[current->get_original_class()];
			RETURN_IF_ERROR(validate(value, schema.field(assignment)->type, r_error));
			for (const auto &property : properties[current]) {
				if (property.name == StringName(assignment)) {
					r_error = "Duplicate resource assignment: " + assignment;
					return ERR_PARSE_ERROR;
				}
			}
			properties[current].push_back({ assignment, value });
			current->set(assignment, value);
			continue;
		}
		if (root_section) {
			r_error = "Unexpected section after [resource].";
			return ERR_PARSE_ERROR;
		}
		if (tag.name == "resource") {
			current = root;
			root_section = true;
		} else if (tag.name == "ext_resource" || tag.name == "sub_resource") {
			if (!tag.fields.has("type") || !tag.fields.has("id")) {
				return ERR_PARSE_ERROR;
			}
			const StringName name = tag.fields["type"];
			const String id = tag.fields["id"];
			const auto *schema = snapshot.classes.getptr(name);
			if (!schema || !schema->concrete) {
				r_error = "Resource class has no compiled field schema: " + String(name);
				return ERR_UNAVAILABLE;
			}
			RETURN_IF_ERROR(declare(name));
			Ref<MissingResource> resource;
			resource.instantiate();
			resource->set_original_class(name);
			resource->set_recording_properties(true);
			if (tag.name == "ext_resource") {
				if (!tag.fields.has("path") || external.has(id)) {
					return ERR_PARSE_ERROR;
				}
				String path = tag.fields["path"];
				if (path.is_relative_path()) {
					path = source.get_base_dir().path_join(path).simplify_path();
				}
				if (!resource_paths.has(path) || schema->cpp_name != String(compiled_resources.get(path, ""))) {
					r_error = "External resource was not compiled with the declared class: " + path;
					return ERR_INVALID_DATA;
				}
				resource->set_path_cache(path);
				uids.insert(resource, tag.fields.has("uid") ? ResourceUID::get_singleton()->text_to_id(tag.fields["uid"]) : ResourceUID::INVALID_ID);
				external.insert(id, resource);
				current.unref();
			} else {
				if (internal.has(id)) {
					return ERR_PARSE_ERROR;
				}
				resource->set_scene_unique_id(id);
				resource->set_path_cache(source + "::" + id);
				internal.insert(id, resource);
				graph.push_back(resource);
				identities.insert(resource.ptr(), graph.size());
				properties.insert(resource, {});
				current = resource;
			}
		} else {
			r_error = "Unsupported resource section: " + tag.name;
			return ERR_PARSE_ERROR;
		}
	}
}

Error WGodotResourceDocument::load(const String &p_path, String &r_error) {
	source = p_path;
	source_revision = WGodotFileStamp::get(source);
	int line = 1;
	const Error result = parse(r_error, line);
	if (result != OK) {
		r_error = source + ":" + itos(line) + ": " + r_error;
	}
	return result;
}

WGodotResourceProtobuf::Reference WGodotResourceDocument::identify(const Ref<Resource> &p_resource) {
	if (p_resource.is_null()) {
		return {};
	}
	if (const uint64_t *id = identities.getptr(p_resource.ptr())) {
		return { *id, 0 };
	}
	return { 0, int64_t(resource_paths[p_resource->get_path()]) };
}

Error WGodotResourceDocument::encode(const String &p_destination, String &r_error) {
	using namespace WGodotResourceProtobuf;
	Error error;
	const Ref<FileAccess> file = FileAccess::open(p_destination, FileAccess::WRITE, &error);
	RETURN_IF_ERROR(error);
	file->store_32(0x42504757);
	file->store_8(1);
	for (const Ref<MissingResource> &resource : graph) {
		const auto &schema = snapshot.classes[resource->get_original_class()];
		Writer fields;
		for (const auto &property : properties[resource]) {
			const auto *field = schema.field(property.name);
			const uint32_t id = field - schema.fields.ptr() + 1;
			if (WGodotResourceEncode::field(fields, id, property.value, field->type, *this, [&](const String &p_path) -> int64_t { return resource_paths.get(p_path, int64_t(0)); }) != OK) {
				r_error = "Cannot encode " + resource->get_original_class() + "." + String(property.name);
				return ERR_INVALID_DATA;
			}
		}
		Writer record;
		record.tag(1, 0);
		record.varint(schema.id);
		record.tag(2, 1);
		record.fixed(schema.fingerprint, 8);
		record.tag(3, 2);
		message(record, fields);
		record.tag(4, 2);
		record.varint(0); // No shared-field links in this document.
		Writer prefix;
		prefix.tag(1, 2);
		prefix.varint(record.data.size());
		file->store_buffer(prefix.data);
		file->store_buffer(record.data);
	}
	file->flush();
	return file->get_error();
}

Error WGodotResourceDocument::cache() const {
	// Binary resources instantiate native bases, then attach external scripts.
	// Reproduce that ordinary layout without loading or executing those scripts.
	WGodotResourceProperties cached_properties;
	HashMap<String, Ref<MissingResource>> script_references;
	Vector<String> original_classes;
	for (const Ref<MissingResource> &resource : graph) {
		const String name = resource->get_original_class();
		const auto &schema = snapshot.classes[name];
		original_classes.push_back(name);
		if (!script_references.has(schema.script)) {
			Ref<MissingResource> script;
			script.instantiate();
			script->set_original_class("GDScript");
			script->set_path_cache(schema.script);
			script_references.insert(schema.script, script);
		}
		List<WGodotResourceProperty> &assignments = cached_properties[resource];
		assignments.push_back({ SNAME("script"), script_references[schema.script] });
		for (const auto &property : properties[resource]) {
			assignments.push_back(property);
		}
		resource->set_original_class(WGodotNamedTypes::native_type(name));
	}
	const Error result = WGodotTextResourceCache(source, source_revision).store(root, cached_properties, uids, scripts);
	for (int i = 0; i < graph.size(); i++) {
		graph[i]->set_original_class(original_classes[i]);
	}
	return result;
}
