// wgodot-changes::file
#include "wgodot_native_resource_export.h"

#include "core/string/string_builder.h"
#include "core/variant/variant_parser.h"

namespace {
using Parser = VariantParser;

struct SourceStream : Parser::Stream {
	const String &source;
	int position = 0;
	bool ended = false;
	SourceStream(const String &p_source) : source(p_source) { readahead_enabled = false; }
	uint32_t _read_buffer(char32_t *p_buffer, uint32_t p_count) override {
		if (position == source.length()) {
			ended = true;
			return 0;
		}
		p_buffer[0] = source[position++];
		return 1;
	}
	bool _is_eof() const override { return ended; }
	bool is_utf8() const override { return false; }
	int offset() const { return position - (saved ? 1 : 0); }
};

struct Token {
	Parser::Token value;
	int begin = 0;
	int end = 0;
};
struct Section {
	int begin = 0;
	int header_end = 0;
	int end = 0;
	String name;
	HashMap<String, int> attributes;
};
struct Edit {
	int begin;
	int end;
	String text;
	bool operator<(const Edit &p_other) const { return begin < p_other.begin; }
};

String attribute(const Section &p_section, const Vector<Token> &p_tokens, const String &p_name) {
	const int *index = p_section.attributes.getptr(p_name);
	return index ? String(p_tokens[*index].value.value) : String();
}

int trivia_end(const String &p_source, int p_start) {
	while (p_start < p_source.length()) {
		if (p_source[p_start] <= ' ') {
			p_start++;
		} else if (p_source[p_start] == ';') {
			while (p_start < p_source.length() && p_source[p_start] != '\n') {
				p_start++;
			}
		} else {
			break;
		}
	}
	return p_start;
}
} // namespace

Error wgodot_export_native_resource(const String &p_source, const Dictionary &p_classes, String &r_output, String &r_error) {
	SourceStream stream(p_source);
	Vector<Token> tokens;
	int line = 1;
	int token_depth = 0;
	while (true) {
		Token token;
		token.begin = trivia_end(p_source, stream.offset());
		// Godot property names are not Variant identifiers (shader_parameter/x,
		// metadata/x, ...). Read the assignment name before tokenizing its value.
		const int line_begin = token.begin == 0 ? 0 : p_source.rfind("\n", token.begin - 1) + 1;
		if (token_depth == 0 && token.begin < p_source.length() && p_source.substr(line_begin, token.begin - line_begin).strip_edges().is_empty() && p_source[token.begin] != '[' && p_source[token.begin] != '"') {
			const int equal = p_source.find("=", token.begin);
			const int newline = p_source.find("\n", token.begin);
			if (equal >= 0 && (newline < 0 || equal < newline)) {
				token.value.type = Parser::TK_IDENTIFIER;
				token.value.value = p_source.substr(token.begin, equal - token.begin).strip_edges();
				token.end = equal;
				line += p_source.substr(stream.offset(), equal - stream.offset()).count("\n");
				stream.position = equal;
				stream.saved = 0;
				tokens.push_back(token);
				continue;
			}
		}
		Error error = Parser::get_token(&stream, token.value, line, r_error);
		if (error != OK) {
			r_error = vformat("Line %d: %s", line, r_error);
			return error;
		}
		if (token.value.type == Parser::TK_EOF) {
			break;
		}
		token.end = stream.offset();
		tokens.push_back(token);
		const auto kind = token.value.type;
		if (kind == Parser::TK_BRACKET_OPEN || kind == Parser::TK_PARENTHESIS_OPEN || kind == Parser::TK_CURLY_BRACKET_OPEN) {
			token_depth++;
		} else if (kind == Parser::TK_BRACKET_CLOSE || kind == Parser::TK_PARENTHESIS_CLOSE || kind == Parser::TK_CURLY_BRACKET_CLOSE) {
			token_depth--;
		}
	}
	Vector<Section> sections;
	int depth = 0;
	for (int i = 0; i < tokens.size(); i++) {
		const auto kind = tokens[i].value.type;
		const String next = i + 1 < tokens.size() && tokens[i + 1].value.type == Parser::TK_IDENTIFIER ? String(tokens[i + 1].value.value) : String();
		if (kind == Parser::TK_BRACKET_OPEN && depth == 0 && (next == "gd_scene" || next == "gd_resource" || next == "node" || next == "resource" || next == "ext_resource" || next == "sub_resource" || next == "connection" || next == "editable")) {
			if (!sections.is_empty()) {
				sections.write[sections.size() - 1].end = i;
			}
			Section section;
			section.begin = i;
			section.end = tokens.size();
			section.name = i + 1 < tokens.size() ? String(tokens[i + 1].value.value) : String();
			sections.push_back(section);
		}
		if (kind == Parser::TK_BRACKET_OPEN || kind == Parser::TK_PARENTHESIS_OPEN || kind == Parser::TK_CURLY_BRACKET_OPEN) {
			depth++;
		} else if (kind == Parser::TK_BRACKET_CLOSE || kind == Parser::TK_PARENTHESIS_CLOSE || kind == Parser::TK_CURLY_BRACKET_CLOSE) {
			depth--;
			if (depth == 0 && !sections.is_empty() && sections[sections.size() - 1].header_end == 0) {
				sections.write[sections.size() - 1].header_end = i;
			}
		}
	}
	if (sections.is_empty() || depth != 0 || (sections[0].name != "gd_scene" && sections[0].name != "gd_resource")) {
		r_error = "Expected a Godot text scene or resource.";
		return ERR_FILE_CORRUPT;
	}
	Vector<Edit> edits;
	HashMap<String, String> script_ids;
	for (Section &section : sections) {
		for (int i = section.begin + 2; i + 2 < section.header_end; i++) {
			if (tokens[i].value.type == Parser::TK_IDENTIFIER && tokens[i + 1].value.type == Parser::TK_EQUAL) {
				section.attributes.insert(tokens[i].value.value, i + 2);
			}
		}
		if (section.name == "gd_resource" && section.attributes.has("script_class")) {
			const int index = section.attributes["script_class"];
			edits.push_back({ tokens[index - 2].begin, tokens[index].end, "" });
		}
		if (section.name == "sub_resource" && attribute(section, tokens, "type") == "GDScript") {
			r_error = "Embedded GDScript must be saved as an external .gd file before native export.";
			return ERR_UNAVAILABLE;
		}
		if (section.name != "ext_resource" || attribute(section, tokens, "type") != "Script") {
			continue;
		}
		const String path = attribute(section, tokens, "path");
		if (!p_classes.has(path)) {
			r_error = "No generated native class for script resource: " + path;
			return ERR_UNAVAILABLE;
		}
		const Dictionary entry = p_classes[path];
		script_ids.insert(attribute(section, tokens, "id"), entry["class"]);
		edits.push_back({ tokens[section.begin].begin, tokens[section.header_end].end, "" });
	}
	for (const Section &section : sections) {
		depth = 0;
		for (int i = section.header_end + 1; i + 1 < section.end; i++) {
			const auto kind = tokens[i].value.type;
			if (depth == 0 && kind == Parser::TK_IDENTIFIER && tokens[i].value.value == Variant("script") && tokens[i + 1].value.type == Parser::TK_EQUAL) {
				if (i + 5 >= section.end || tokens[i + 2].value.value != Variant("ExtResource") || tokens[i + 3].value.type != Parser::TK_PARENTHESIS_OPEN || tokens[i + 5].value.type != Parser::TK_PARENTHESIS_CLOSE || !script_ids.has(tokens[i + 4].value.value)) {
					r_error = "Native export requires an external generated script attachment; script overrides/removal are not supported.";
					return ERR_UNAVAILABLE;
				}
				const Section &target = section.name == "resource" ? sections[0] : section;
				if (!target.attributes.has("type") || target.attributes.has("instance")) {
					r_error = "A script override on an inherited/instanced scene needs native scene flattening, which is not supported yet.";
					return ERR_UNAVAILABLE;
				}
				const int type_index = target.attributes["type"];
				const String native_class = script_ids[tokens[i + 4].value.value];
				edits.push_back({ tokens[type_index].begin, tokens[type_index].end, "\"" + native_class + "\"" });
				edits.push_back({ tokens[i].begin, tokens[i + 5].end, "" });
				i += 5;
				continue;
			}
			if (kind == Parser::TK_BRACKET_OPEN || kind == Parser::TK_PARENTHESIS_OPEN || kind == Parser::TK_CURLY_BRACKET_OPEN) {
				depth++;
			} else if (kind == Parser::TK_BRACKET_CLOSE || kind == Parser::TK_PARENTHESIS_CLOSE || kind == Parser::TK_CURLY_BRACKET_CLOSE) {
				depth--;
			}
		}
	}
	// Script references may also be container type arguments. A script used as
	// an ordinary resource value has different semantics and must not disappear.
	Vector<bool> type_scopes;
	for (int i = 0; i < tokens.size(); i++) {
		const auto kind = tokens[i].value.type;
		if (kind == Parser::TK_BRACKET_OPEN || kind == Parser::TK_PARENTHESIS_OPEN || kind == Parser::TK_CURLY_BRACKET_OPEN) {
			type_scopes.push_back(kind == Parser::TK_BRACKET_OPEN && i > 0 && (tokens[i - 1].value.value == Variant("Array") || tokens[i - 1].value.value == Variant("Dictionary")));
		} else if (kind == Parser::TK_BRACKET_CLOSE || kind == Parser::TK_PARENTHESIS_CLOSE || kind == Parser::TK_CURLY_BRACKET_CLOSE) {
			if (!type_scopes.is_empty()) {
				type_scopes.resize(type_scopes.size() - 1);
			}
		} else if (i + 3 < tokens.size() && tokens[i].value.value == Variant("ExtResource") && tokens[i + 1].value.type == Parser::TK_PARENTHESIS_OPEN && script_ids.has(tokens[i + 2].value.value)) {
			bool removed = false;
			for (const Edit &edit : edits) {
				removed |= edit.begin <= tokens[i].begin && edit.end >= tokens[i + 3].end;
			}
			if (!removed) {
				if (type_scopes.is_empty() || !type_scopes[type_scopes.size() - 1]) {
					r_error = "A Script resource is used as a value; native export only supports attachments and container type references.";
					return ERR_UNAVAILABLE;
				}
				edits.push_back({ tokens[i].begin, tokens[i + 3].end, script_ids[tokens[i + 2].value.value] });
			}
		}
	}
	edits.sort();
	StringBuilder output;
	int position = 0;
	for (const Edit &edit : edits) {
		if (edit.begin < position) {
			r_error = "Overlapping native resource substitutions.";
			return ERR_FILE_CORRUPT;
		}
		output += p_source.substr(position, edit.begin - position);
		output += edit.text;
		position = edit.end;
	}
	output += p_source.substr(position);
	r_output = output.as_string();
	return OK;
}
