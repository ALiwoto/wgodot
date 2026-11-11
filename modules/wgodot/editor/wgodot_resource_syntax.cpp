// wgodot-changes::file
#include "wgodot_resource_syntax.h"

#include "wgodot_export_target.h"
#include "wgodot_resource_check.h"
#include "wgodot_resource_validation.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"

namespace {
struct ProjectResources {
	const WGodotExportTarget &target;
	Vector<String> paths;
	Error collect(const String &p_path) {
		Error error = OK;
		Ref<DirAccess> directory = DirAccess::open(p_path, &error);
		if (directory.is_null()) {
			return error;
		}
		if (directory->file_exists(".gdignore")) {
			return OK;
		}
		RETURN_IF_ERROR(directory->list_dir_begin());
		for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
			if (name.begins_with(".")) {
				continue;
			}
			const String path = p_path.path_join(name);
			if (directory->current_is_dir()) {
				ERR_FAIL_COND_V_MSG(directory->is_link(name), ERR_UNAVAILABLE, "Resource checking does not follow directory symlinks: " + path);
				RETURN_IF_ERROR(collect(path));
			} else if (target.includes(path) && (name.get_extension() == "tres" || name.get_extension() == "tscn")) {
				paths.push_back(path);
			}
		}
		return OK;
	}
};
} //namespace

Error WGodotResourceSyntax::check_project(const WGodotResourceTypes &p_types, const WGodotExportTarget &p_target, Vector<String> &r_diagnostics) {
	ProjectResources check{ p_target };
	RETURN_IF_ERROR(check.collect("res://"));
	Ref<WGodotResourceCheck> validation;
	validation.instantiate();
	validation->start(check.paths, p_types);
	Dictionary result = validation->poll();
	while (result.is_empty()) {
		OS::get_singleton()->delay_usec(1000);
		result = validation->poll();
	}
	const Array diagnostics = result["diagnostics"];
	for (const Dictionary &diagnostic : diagnostics) {
		if (!bool(diagnostic["warning"])) {
			r_diagnostics.push_back(String(diagnostic["path"]) + ":" + itos(diagnostic["line"]) + ": " + String(diagnostic["message"]));
		}
	}
	print_line(vformat("Resource types: %d parsed, %d cached in %d ms", result["resource_parsed_count"], result["resource_cached_count"], result["resource_elapsed_msec"]));
	return int(result["resource_error_count"]) == 0 ? OK : ERR_PARSE_ERROR;
}

#include "core/io/wgodot_named_types.h"
#include "core/variant/variant_parser.h"
#include "scene/resources/resource_format_text.h"

#include "modules/wgodot/codecs/resource_codec.h"

#include <initializer_list>

namespace {

Error parse_reference(void *, VariantParser::Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_message) {
	VariantParser::Token token;
	RETURN_IF_ERROR(VariantParser::get_token(p_stream, token, r_line, r_message));
	if (token.type != VariantParser::TK_STRING && token.type != VariantParser::TK_NUMBER) {
		r_message = "Expected a resource ID (string or legacy number).";
		return ERR_PARSE_ERROR;
	}
	RETURN_IF_ERROR(VariantParser::get_token(p_stream, token, r_line, r_message));
	if (token.type != VariantParser::TK_PARENTHESIS_CLOSE) {
		r_message = "Expected ')' after resource ID.";
		return ERR_PARSE_ERROR;
	}
	r_resource.unref();
	return OK;
}

// Look only between complete parser records. Brackets/newlines inside strings,
// arrays, dictionaries and codec payloads cannot produce formatting warnings.
bool next_record(VariantParser::Stream &p_stream, int &r_line) {
	while (true) {
		char32_t character = p_stream.saved;
		p_stream.saved = 0;
		if (!character) {
			character = p_stream.get_char();
		}
		if (p_stream.is_eof()) {
			return false;
		}
		if (character == ';') {
			while (character != '\n' && !p_stream.is_eof()) {
				character = p_stream.get_char();
			}
		}
		if (character == '\n') {
			r_line++;
		} else if (character > ' ') {
			p_stream.saved = character;
			return true;
		}
	}
}

Error require_fields(const VariantParser::Tag &p_tag, std::initializer_list<const char *> p_fields, String &r_message) {
	for (const char *field : p_fields) {
		if (!p_tag.fields.has(field)) {
			r_message = vformat("Missing '%s' in [%s].", field, p_tag.name);
			return ERR_PARSE_ERROR;
		}
	}
	return OK;
}

} // namespace

WGodotResourceSyntax::Result WGodotResourceSyntax::check(const Ref<FileAccess> &p_file, const WGodotResourceTypes *p_types, HashSet<String> *p_scene_stack, bool p_collect_properties) {
	Result result;
	WGodotResourceValidation validation(p_file->get_path(), p_types, result, p_scene_stack, p_collect_properties);
	VariantParser::StreamFile stream;
	stream.f = p_file;
	VariantParser::ResourceParser resources;
	resources.ext_func = parse_reference;
	resources.sub_func = parse_reference;
	resources.skip_class_loading = true;
	resources.syntax_only = true;
	validation.configure(resources);
	VariantParser::Tag tag;
	result.error = VariantParser::parse_tag(&stream, result.line, result.message, tag, &resources);
	if (result.error != OK) {
		return result;
	}
	const bool scene = tag.name == "gd_scene";
	if (!scene && tag.name != "gd_resource") {
		result.error = ERR_PARSE_ERROR;
		result.message = "Expected [gd_scene] or [gd_resource] header.";
		return result;
	}
	if (p_types) {
		for (const auto &field : tag.fields) {
			const bool integer = field.key == "load_steps" || field.key == "format";
			const bool string = field.key == "type" || field.key == "uid" || field.key == "script_class";
			if ((!integer && !string) || field.value.get_type() != (integer ? Variant::INT : Variant::STRING)) {
				result.error = ERR_PARSE_ERROR;
				result.message = "Unknown or incorrectly typed resource header field: " + field.key;
				return result;
			}
		}
	}
	if (!scene) {
		result.error = require_fields(tag, { "type" }, result.message);
		if (result.error != OK) {
			return result;
		}
	}
	if (tag.fields.has("format") && int(tag.fields["format"]) > ResourceLoaderText::FORMAT_VERSION) {
		result.error = ERR_PARSE_ERROR;
		result.message = "Resource format is newer than this engine supports.";
		return result;
	}
	const String resource_type = scene ? String("PackedScene") : String(tag.fields["type"]);
	result.error = validation.header(resource_type, result.message);
	if (result.error != OK) {
		return result;
	}
	StringName assignment_type;
	bool allows_properties = false;
	bool has_body = false;
	bool main_resource = false;
	bool nested_scene = false;
	bool internal_resources = false;
	while (true) {
		const int previous_line = result.line;
		if (!next_record(stream, result.line)) {
			if (!has_body) {
				result.error = ERR_PARSE_ERROR;
				result.message = scene ? "Scene has no nodes." : "Missing [resource] section.";
			}
			if (result.error == OK) {
				result.error = validation.finish(result.message);
			}
			return result;
		}
		const int record_line = result.line;
		const bool declaration = stream.saved == '[';
		if (record_line == previous_line) {
			result.missing_newlines.push_back(record_line);
		}
		String assignment;
		Variant value;
		tag = VariantParser::Tag();
		result.error = VariantParser::parse_tag_assign_eof(&stream, result.line, result.message, tag, assignment, value, &resources, false, &validation);
		if (result.error != OK) {
			// EOF is only a successful terminator between complete records.
			if (result.error == ERR_FILE_EOF) {
				result.error = ERR_PARSE_ERROR;
				result.message = "Unexpected end of file in resource declaration or assignment.";
			}
			return result;
		}
		if (!declaration) {
			if (!allows_properties || assignment.is_empty()) {
				result.error = ERR_PARSE_ERROR;
				result.message = "Unexpected property assignment outside a resource or node.";
				return result;
			}
			if (assignment == "script") {
				result.error = ERR_PARSE_ERROR;
				result.line = record_line;
				result.message = "Script assignments are forbidden. Directly use the class_name name on the type attribute.";
				return result;
			}
			continue;
		}
		allows_properties = false;
		assignment_type = StringName();
		if (tag.name == "ext_resource" && !main_resource && !internal_resources) {
			result.error = require_fields(tag, { "type", "path", "id" }, result.message);
			if (result.error == OK && WGodotNamedTypes::is_script_reference(tag.fields["type"], tag.fields["path"])) {
				result.error = ERR_PARSE_ERROR;
				result.message = "Script ext_resource declarations are forbidden. Directly use the class_name name on the type attribute.";
			}
		} else if (tag.name == "sub_resource" && !main_resource) {
			internal_resources = true;
			result.error = require_fields(tag, { "type", "id" }, result.message);
			if (result.error == OK) {
				assignment_type = String(tag.fields["type"]);
				allows_properties = true;
				nested_scene = WGodotNamedTypes::native_type(assignment_type) == SNAME("PackedScene");
			}
		} else if (tag.name == "resource" && !main_resource) {
			assignment_type = resource_type;
			allows_properties = true;
			main_resource = true;
			has_body = !scene;
			nested_scene = false;
		} else if (tag.name == "node" && (scene || nested_scene)) {
			result.error = require_fields(tag, { "name" }, result.message);
			allows_properties = true;
			if (!nested_scene) {
				has_body = true;
				main_resource = true;
			}
		} else if (tag.name == "connection" && (scene || nested_scene)) {
			result.error = require_fields(tag, { "from", "to", "signal", "method" }, result.message);
		} else if (tag.name == "editable" && (scene || nested_scene)) {
			result.error = require_fields(tag, { "path" }, result.message);
		} else {
			result.error = ERR_PARSE_ERROR;
			result.message = vformat("Unexpected [%s] section.", tag.name);
		}
		if (result.error == OK) {
			result.error = validation.tag(tag, resource_type, result.message);
		}
		if (result.error != OK) {
			result.line = record_line;
			return result;
		}
	}
}
