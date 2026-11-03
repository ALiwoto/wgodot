// wgodot-changes::file
#include "wgodot_resource_syntax.h"

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

WGodotResourceSyntax::Result WGodotResourceSyntax::check(const Ref<FileAccess> &p_file) {
	Result result;
	VariantParser::StreamFile stream;
	stream.f = p_file;
	VariantParser::ResourceParser resources;
	resources.ext_func = parse_reference;
	resources.sub_func = parse_reference;
	resources.skip_class_loading = true;
	resources.syntax_only = true;
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
		WResourceCodec::Assignment codec(nullptr, assignment_type);
		result.error = VariantParser::parse_tag_assign_eof(&stream, result.line, result.message, tag, assignment, value, &resources, false, &codec);
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
		if (result.error != OK) {
			result.line = record_line;
			return result;
		}
	}
}
