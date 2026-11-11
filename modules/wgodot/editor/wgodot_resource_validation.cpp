// wgodot-changes::file
#include "wgodot_resource_validation.h"

#include "core/io/resource_loader.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/io/wgodot_global_resources.h"

#include "modules/wgodot/codecs/resource_codec.h"
#include "modules/wgodot/serialization/wire.h"

using VP = VariantParser;

WGodotResourceValidation::WGodotResourceValidation(const String &p_path, const WGodotResourceTypes *p_types, WGodotResourceSyntax::Result &r_result, HashSet<String> *p_scene_stack, bool p_collect_properties) :
		path(p_path), types(p_types), result(r_result), scene_stack(p_scene_stack ? p_scene_stack : &owned_scene_stack), collect_properties(p_collect_properties) {
	if (types) {
		global_paths = WGlobalResources::paths();
	}
}

Error WGodotResourceValidation::fail(const String &p_message, String &r_error) const {
	r_error = current_property.is_empty() ? p_message : current_property + ": " + p_message;
	return ERR_PARSE_ERROR;
}

Error WGodotResourceValidation::token(Stream *p_stream, Token &r_token, int &r_line, String &r_error) const {
	return VP::get_token(p_stream, r_token, r_line, r_error);
}

Error WGodotResourceValidation::expect(Stream *p_stream, TokenType p_type, int &r_line, String &r_error) const {
	Token next;
	RETURN_IF_ERROR(token(p_stream, next, r_line, r_error));
	return next.type == p_type ? OK : fail("Unexpected token in typed resource value.", r_error);
}

char32_t WGodotResourceValidation::peek(Stream *p_stream, int &r_line) const {
	char32_t next = p_stream->saved;
	p_stream->saved = 0;
	if (!next) {
		next = p_stream->get_char();
	}
	while (next && next <= ' ') {
		if (next == '\n') {
			r_line++;
		}
		next = p_stream->get_char();
	}
	p_stream->saved = next;
	return next;
}

Error WGodotResourceValidation::class_type(const StringName &p_type, const StringName &p_base, String &r_error) {
	result.declarations.insert(p_type, types->signature(p_type));
	if (!types->exists(p_type) || !types->derives(p_type, p_base) || !types->instantiable(p_type)) {
		return fail("Expected a concrete " + String(p_base) + " class, got '" + String(p_type) + "'.", r_error);
	}
	if (p_base == SNAME("Resource") && !types->has_resource_exports(p_type)) {
		return fail("Resource class '" + String(p_type) + "' has no public @export fields; no resource codec is generated for it.", r_error);
	}
	return types->check_exports(p_type, r_error);
}

Error WGodotResourceValidation::field(const StringName &p_class, const StringName &p_name, Type &r_type, String &r_error) {
	WGodotResourceTypes::Field declaration;
	if (!types->field(p_class, p_name, declaration, result.declarations)) {
		return fail("Unknown field '" + String(p_name) + "' on " + String(p_class) + ". Dynamic properties are forbidden.", r_error);
	}
	if (!declaration.exposed) {
		return fail(String(p_class) + "." + String(p_name) + " must be a public @export instance field.", r_error);
	}
	if (!declaration.type.concrete()) {
		return fail(String(p_class) + "." + String(p_name) + " must have a concrete type, including every container element/key/value; got " + declaration.type.text() + ".", r_error);
	}
	r_type = declaration.type;
	return OK;
}

Error WGodotResourceValidation::resource_type(const String &p_path, StringName &r_type, String &r_error) {
	String source = p_path.is_relative_path() ? path.get_base_dir().path_join(p_path).simplify_path() : p_path.simplify_path();
	if (const StringName *known = resource_headers.getptr(source)) {
		r_type = *known;
		return OK;
	}
	result.dependencies.insert(source, WGodotFileStamp::get(source));
	if (!FileAccess::exists(source)) {
		return fail("Resource does not exist: " + source, r_error);
	}
	if (source.get_extension() == "tres" || source.get_extension() == "tscn") {
		VP::StreamFile stream;
		stream.f = FileAccess::open(source, FileAccess::READ);
		if (stream.f.is_null()) {
			return fail("Cannot read resource declaration: " + source, r_error);
		}
		VP::Tag tag;
		int line = 1;
		RETURN_IF_ERROR(VP::parse_tag(&stream, line, r_error, tag));
		if (tag.name == "gd_scene") {
			r_type = SNAME("PackedScene");
		} else if (tag.name == "gd_resource" && tag.fields.has("type")) {
			r_type = String(tag.fields["type"]);
		} else {
			return fail("Invalid resource header: " + source, r_error);
		}
	} else {
		r_type = ResourceLoader::get_resource_type(source);
	}
	RETURN_IF_ERROR(class_type(r_type, StringName("Resource"), r_error));
	if (types->derives(r_type, SNAME("Script"))) {
		return fail("Stored Script values are forbidden; declare resource classes directly.", r_error);
	}
	resource_headers.insert(source, r_type);
	return OK;
}

Error WGodotResourceValidation::annotation(Token p_token, Stream *p_stream, Type &r_type, int &r_line, String &r_error) {
	if (p_token.type != VP::TK_IDENTIFIER) {
		return fail("Expected a concrete type name.", r_error);
	}
	const String name = p_token.value;
	r_type.builtin = Variant::get_type_by_name(name);
	if (r_type.builtin == Variant::VARIANT_MAX && types->exists(name)) {
		r_type.builtin = Variant::OBJECT;
		r_type.name = name;
		result.declarations.insert(name, types->signature(name));
	}
	if (r_type.builtin == Variant::ARRAY || r_type.builtin == Variant::DICTIONARY) {
		RETURN_IF_ERROR(expect(p_stream, VP::TK_BRACKET_OPEN, r_line, r_error));
		const int count = r_type.builtin == Variant::ARRAY ? 1 : 2;
		for (int i = 0; i < count; i++) {
			if (i) {
				RETURN_IF_ERROR(expect(p_stream, VP::TK_COMMA, r_line, r_error));
			}
			RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
			Type element;
			RETURN_IF_ERROR(annotation(p_token, p_stream, element, r_line, r_error));
			r_type.elements.push_back(element);
		}
		RETURN_IF_ERROR(expect(p_stream, VP::TK_BRACKET_CLOSE, r_line, r_error));
	}
	return r_type.concrete() ? OK : fail("Dynamic or unsupported type: " + name, r_error);
}

Error WGodotResourceValidation::container(Stream *p_stream, const Type &p_type, int &r_line, String &r_error, int p_depth) {
	const bool dictionary = p_type.builtin == Variant::DICTIONARY;
	const Type element = p_type.builtin >= Variant::PACKED_BYTE_ARRAY ? Type::packed_element(p_type.builtin) : p_type.elements[0];
	const TokenType end = dictionary ? VP::TK_CURLY_BRACKET_CLOSE : VP::TK_BRACKET_CLOSE;
	Token next;
	RETURN_IF_ERROR(token(p_stream, next, r_line, r_error));
	while (next.type != end) {
		Type actual;
		RETURN_IF_ERROR(value(next, p_stream, element, actual, r_line, r_error, p_depth + 1));
		if (p_type.builtin == Variant::PACKED_BYTE_ARRAY || p_type.builtin == Variant::PACKED_INT32_ARRAY) {
			const int64_t number = next.value;
			if (next.type != VP::TK_NUMBER || (p_type.builtin == Variant::PACKED_BYTE_ARRAY ? number < 0 || number > UINT8_MAX : number < INT32_MIN || number > INT32_MAX)) {
				return fail("Packed array element is out of range.", r_error);
			}
		}
		if (dictionary) {
			RETURN_IF_ERROR(expect(p_stream, VP::TK_COLON, r_line, r_error));
			RETURN_IF_ERROR(token(p_stream, next, r_line, r_error));
			RETURN_IF_ERROR(value(next, p_stream, p_type.elements[1], actual, r_line, r_error, p_depth + 1));
		}
		RETURN_IF_ERROR(token(p_stream, next, r_line, r_error));
		if (next.type == end) {
			break;
		}
		if (next.type != VP::TK_COMMA) {
			return fail("Expected a comma between container entries.", r_error);
		}
		RETURN_IF_ERROR(token(p_stream, next, r_line, r_error));
	}
	return OK;
}

Error WGodotResourceValidation::reference(Stream *p_stream, const String &p_kind, Type &r_type, int &r_line, String &r_error) {
	RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_OPEN, r_line, r_error));
	Token id;
	RETURN_IF_ERROR(token(p_stream, id, r_line, r_error));
	if (id.type != VP::TK_STRING && id.type != VP::TK_NUMBER) {
		return fail("Resource references require a literal name or ID.", r_error);
	}
	const String name = id.value;
	RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_CLOSE, r_line, r_error));
	r_type.builtin = Variant::OBJECT;
	if (p_kind == "GlobalResource") {
		const String *source = global_paths.getptr(name);
		if (!source) {
			return fail("Unknown global resource: " + name, r_error);
		}
		RETURN_IF_ERROR(resource_type(*source, r_type.name, r_error));
		if (peek(p_stream, r_line) == '.') {
			RETURN_IF_ERROR(expect(p_stream, VP::TK_PERIOD, r_line, r_error));
			RETURN_IF_ERROR(token(p_stream, id, r_line, r_error));
			if (id.type != VP::TK_IDENTIFIER) {
				return fail("Expected a declared global resource field.", r_error);
			}
			const StringName owner = r_type.name;
			RETURN_IF_ERROR(field(owner, String(id.value), r_type, r_error));
		}
	} else {
		const Reference *source = p_kind == "ExtResource" ? external.getptr(name) : internal.getptr(name);
		if (!source) {
			return fail("Unknown " + p_kind + " ID: " + name, r_error);
		}
		r_type.name = source->type;
	}
	return OK;
}

Error WGodotResourceValidation::value(Token p_token, Stream *p_stream, const Type &p_expected, Type &r_actual, int &r_line, String &r_error, int p_depth) {
	r_actual = Type();
	if (p_depth > 64) {
		return fail("Resource value nesting is too deep.", r_error);
	}
	if (p_expected.resource_path && p_token.type != VP::TK_STRING) {
		return fail("WResPath requires a literal resource path.", r_error);
	}
	if (p_token.type == VP::TK_BRACKET_OPEN || p_token.type == VP::TK_CURLY_BRACKET_OPEN) {
		const Variant::Type kind = p_token.type == VP::TK_BRACKET_OPEN ? Variant::ARRAY : Variant::DICTIONARY;
		const bool packed = kind == Variant::ARRAY && p_expected.builtin >= Variant::PACKED_BYTE_ARRAY;
		if ((!packed && p_expected.builtin != kind) || !p_expected.concrete()) {
			return fail("Container literal requires a fully typed " + Variant::get_type_name(kind) + " destination.", r_error);
		}
		r_actual = p_expected;
		RETURN_IF_ERROR(container(p_stream, r_actual, r_line, r_error, p_depth));
	} else if (p_token.type == VP::TK_NUMBER || p_token.type == VP::TK_STRING || p_token.type == VP::TK_STRING_NAME || p_token.type == VP::TK_COLOR) {
		r_actual.builtin = p_token.value.get_type();
		if (p_expected.resource_path) {
			if (r_actual.builtin != Variant::STRING) {
				return fail("WResPath requires a literal resource path.", r_error);
			}
			if (!String(p_token.value).is_empty()) {
				StringName resource;
				RETURN_IF_ERROR(resource_type(p_token.value, resource, r_error));
			}
		}
	} else if (p_token.type == VP::TK_IDENTIFIER) {
		const String name = p_token.value;
		if (name == "repeat") {
			const bool packed = p_expected.builtin >= Variant::PACKED_BYTE_ARRAY && p_expected.builtin < Variant::VARIANT_MAX;
			if (!p_expected.concrete() || (!packed && p_expected.builtin != Variant::ARRAY)) {
				return fail("repeat requires a fully typed array destination.", r_error);
			}
			RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_OPEN, r_line, r_error));
			RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
			const Type element = packed ? Type::packed_element(p_expected.builtin) : p_expected.elements[0];
			Type actual;
			RETURN_IF_ERROR(value(p_token, p_stream, element, actual, r_line, r_error, p_depth + 1));
			if (p_expected.builtin == Variant::PACKED_BYTE_ARRAY || p_expected.builtin == Variant::PACKED_INT32_ARRAY) {
				const int64_t number = p_token.value;
				if (p_token.type != VP::TK_NUMBER || (p_expected.builtin == Variant::PACKED_BYTE_ARRAY ? number < 0 || number > UINT8_MAX : number < INT32_MIN || number > INT32_MAX)) {
					return fail("Repeated packed array element is out of range.", r_error);
				}
			}
			int count;
			RETURN_IF_ERROR(WGodotResourceValueParser::parse_repeat_count(p_stream, count, r_line, r_error));
			r_actual = p_expected;
		} else if (name == "true" || name == "false") {
			r_actual.builtin = Variant::BOOL;
		} else if (name == "null" || name == "nil") {
			r_actual.builtin = Variant::NIL;
		} else if (name == "ExtResource" || name == "SubResource" || name == "GlobalResource") {
			RETURN_IF_ERROR(reference(p_stream, name, r_actual, r_line, r_error));
		} else if (name == "Array" || name == "Dictionary") {
			RETURN_IF_ERROR(annotation(p_token, p_stream, r_actual, r_line, r_error));
			if (!types->accepts(p_expected, r_actual)) {
				return fail("Expected " + p_expected.text() + ", got " + r_actual.text() + ".", r_error);
			}
			RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_OPEN, r_line, r_error));
			RETURN_IF_ERROR(expect(p_stream, r_actual.builtin == Variant::ARRAY ? VP::TK_BRACKET_OPEN : VP::TK_CURLY_BRACKET_OPEN, r_line, r_error));
			RETURN_IF_ERROR(container(p_stream, p_expected, r_line, r_error, p_depth));
			RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_CLOSE, r_line, r_error));
		} else {
			r_actual.builtin = Variant::get_type_by_name(name);
			if (r_actual.builtin == Variant::VARIANT_MAX || r_actual.builtin == Variant::NIL || r_actual.builtin == Variant::OBJECT || r_actual.builtin == Variant::RID || r_actual.builtin == Variant::CALLABLE || r_actual.builtin == Variant::SIGNAL) {
				return fail("Dynamic or unsupported constructor: " + name, r_error);
			}
			RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_OPEN, r_line, r_error));
			if (r_actual.builtin == Variant::NODE_PATH || r_actual.builtin == Variant::STRING_NAME || r_actual.builtin == Variant::STRING) {
				RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
				if (p_token.type != VP::TK_STRING) {
					return fail(name + " requires a literal string.", r_error);
				}
				if (r_actual.builtin == Variant::NODE_PATH && p_expected.builtin == Variant::OBJECT && types->derives(p_expected.name, SNAME("Node"))) {
					if (!node_section) {
						return fail("Node references require a scene node context.", r_error);
					}
					node_bindings.push_back({ nested_scene ? nested_scene_id : String(), owner_node, String(p_token.value), p_expected.name, r_line });
					r_actual = p_expected;
				}
				RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_CLOSE, r_line, r_error));
			} else {
				// Packed numeric payloads are checked as a stream. No duplicate arrays
				// or live resource instances are allocated during validation.
				const bool packed = r_actual.builtin >= Variant::PACKED_BYTE_ARRAY;
				const bool strings = r_actual.builtin == Variant::PACKED_STRING_ARRAY;
				const bool integers = r_actual.builtin == Variant::PACKED_BYTE_ARRAY || r_actual.builtin == Variant::PACKED_INT32_ARRAY || r_actual.builtin == Variant::PACKED_INT64_ARRAY || r_actual.builtin == Variant::VECTOR2I || r_actual.builtin == Variant::VECTOR3I || r_actual.builtin == Variant::VECTOR4I || r_actual.builtin == Variant::RECT2I;
				int count = 0;
				RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
				if (r_actual.builtin == Variant::PACKED_BYTE_ARRAY && p_token.type == VP::TK_STRING) {
					PackedByteArray decoded;
					if (!WGodotSerialization::decode_base64(p_token.value, decoded)) {
						return fail("Invalid PackedByteArray base64 value.", r_error);
					}
					RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_CLOSE, r_line, r_error));
					return types->accepts(p_expected, r_actual) ? OK : fail("Expected " + p_expected.text() + ", got PackedByteArray.", r_error);
				}
				while (p_token.type != VP::TK_PARENTHESIS_CLOSE) {
					if (strings ? p_token.type != VP::TK_STRING : p_token.type != VP::TK_NUMBER) {
						return fail(name + " contains an incompatible literal.", r_error);
					}
					if (integers && p_token.value.get_type() != Variant::INT) {
						return fail(name + " requires integer literals.", r_error);
					}
					if (r_actual.builtin == Variant::PACKED_BYTE_ARRAY && (int64_t(p_token.value) < 0 || int64_t(p_token.value) > UINT8_MAX)) {
						return fail("PackedByteArray element is out of range.", r_error);
					}
					if (r_actual.builtin == Variant::PACKED_INT32_ARRAY && (int64_t(p_token.value) < INT32_MIN || int64_t(p_token.value) > INT32_MAX)) {
						return fail("PackedInt32Array element is out of range.", r_error);
					}
					count++;
					RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
					if (p_token.type == VP::TK_PARENTHESIS_CLOSE) {
						break;
					}
					if (p_token.type != VP::TK_COMMA) {
						return fail("Expected a comma in " + name + ".", r_error);
					}
					RETURN_IF_ERROR(token(p_stream, p_token, r_line, r_error));
				}
				int width = 0;
				switch (r_actual.builtin) {
					case Variant::VECTOR2:
					case Variant::VECTOR2I:
					case Variant::PACKED_VECTOR2_ARRAY:
						width = 2;
						break;
					case Variant::VECTOR3:
					case Variant::VECTOR3I:
					case Variant::PACKED_VECTOR3_ARRAY:
						width = 3;
						break;
					case Variant::VECTOR4:
					case Variant::VECTOR4I:
					case Variant::RECT2:
					case Variant::RECT2I:
					case Variant::PLANE:
					case Variant::QUATERNION:
					case Variant::COLOR:
					case Variant::PACKED_VECTOR4_ARRAY:
					case Variant::PACKED_COLOR_ARRAY:
						width = 4;
						break;
					case Variant::TRANSFORM2D:
					case Variant::AABB:
						width = 6;
						break;
					case Variant::BASIS:
						width = 9;
						break;
					case Variant::TRANSFORM3D:
						width = 12;
						break;
					case Variant::PROJECTION:
						width = 16;
						break;
					default:
						width = packed ? 1 : 0;
						break;
				}
				if (!width || (packed ? count % width != 0 : count != width)) {
					return fail("Invalid component count for " + name + ".", r_error);
				}
			}
		}
	} else {
		return fail("Expected a statically typed resource value.", r_error);
	}
	return types->accepts(p_expected, r_actual) ? OK : fail("Expected " + p_expected.text() + ", got " + r_actual.text() + ".", r_error);
}

Error WGodotResourceValidation::parse(const String &p_name, Token &p_token, Stream *p_stream, int &r_line, String &r_error, VP::ResourceParser *p_resources) {
	current_property = String(owner_type) + "." + p_name;
	if (assignments.has(p_name)) {
		return fail("Duplicate assignment.", r_error);
	}
	assignments.insert(p_name);
	if (collect_properties && !node_section) {
		result.properties[resource_section].insert(p_name);
	}
	if (types) {
		const WResourceCodec *codec = WResourceCodec::find(owner_type);
		if (codec && codec->owns_assignment(p_name)) {
			handled = true;
			return codec->check_types(p_token, p_stream, r_line, r_error, p_resources);
		}
	}
	WResourceCodec::Assignment codec(nullptr, owner_type);
	RETURN_IF_ERROR(codec.parse(p_name, p_token, p_stream, r_line, r_error, p_resources));
	if (codec.handled) {
		handled = true;
		return OK;
	}
	if (!types) {
		return OK;
	}
	handled = true;
	Type expected;
	RETURN_IF_ERROR(field(owner_type, p_name, expected, r_error));
	Type actual;
	return value(p_token, p_stream, expected, actual, r_line, r_error);
}

Error WGodotResourceValidation::marker(Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error, bool p_external) {
	Token id;
	RETURN_IF_ERROR(token(p_stream, id, r_line, r_error));
	if (id.type != VP::TK_STRING && id.type != VP::TK_NUMBER) {
		return fail("Expected a literal resource ID.", r_error);
	}
	const String name = id.value;
	RETURN_IF_ERROR(expect(p_stream, VP::TK_PARENTHESIS_CLOSE, r_line, r_error));
	const Reference *reference = p_external ? external.getptr(name) : internal.getptr(name);
	if (!reference) {
		return fail("Unknown scene instance resource: " + name, r_error);
	}
	// A private marker carries only a declared reference through tag parsing.
	// It never instantiates the referenced class or executes project code.
	r_resource.instantiate();
	r_resource->set_meta(SNAME("reference"), p_external ? name : "::" + name);
	return OK;
}

Error WGodotResourceValidation::parse_external(void *p_self, Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error) {
	return static_cast<WGodotResourceValidation *>(p_self)->marker(p_stream, r_resource, r_line, r_error, true);
}

Error WGodotResourceValidation::parse_internal(void *p_self, Stream *p_stream, Ref<Resource> &r_resource, int &r_line, String &r_error) {
	return static_cast<WGodotResourceValidation *>(p_self)->marker(p_stream, r_resource, r_line, r_error, false);
}

void WGodotResourceValidation::configure(VP::ResourceParser &r_resources) {
	if (types) {
		r_resources.userdata = this;
		r_resources.ext_func = parse_external;
		r_resources.sub_func = parse_internal;
	}
}

Error WGodotResourceValidation::header(const StringName &p_type, String &r_error) {
	return types ? class_type(p_type, SNAME("Resource"), r_error) : OK;
}

Error WGodotResourceValidation::instance(const Reference &p_reference, const String &p_prefix, String &r_error) {
	if (p_reference.type != SNAME("PackedScene")) {
		return fail("A scene instance must reference a PackedScene.", r_error);
	}
	HashMap<String, StringName> nodes;
	if (p_reference.path.begins_with("::")) {
		const auto *source = nested_nodes.getptr(p_reference.path.substr(2));
		if (!source) {
			return fail("Embedded scene has no declared nodes.", r_error);
		}
		nodes = *source;
	} else {
		if (!scenes.has(p_reference.path)) {
			if (scene_stack->has(p_reference.path) || p_reference.path == path) {
				return fail("Cyclic scene instance: " + p_reference.path, r_error);
			}
			scene_stack->insert(path);
			Ref<FileAccess> file = FileAccess::open(p_reference.path, FileAccess::READ);
			if (file.is_null()) {
				return fail("Cannot read scene instance: " + p_reference.path, r_error);
			}
			const auto checked = WGodotResourceSyntax::check(file, types, scene_stack);
			scene_stack->erase(path);
			if (checked.error != OK) {
				return fail(p_reference.path + ":" + itos(checked.line) + ": " + checked.message, r_error);
			}
			for (const auto &dependency : checked.dependencies) {
				result.dependencies.insert(dependency.key, dependency.value);
			}
			for (const auto &declaration : checked.declarations) {
				result.declarations.insert(declaration.key, declaration.value);
			}
			scenes.insert(p_reference.path, checked);
		}
		nodes = scenes[p_reference.path].nodes;
	}
	for (const auto &node : nodes) {
		const String name = node.key == "." ? p_prefix : p_prefix == "." ? node.key
																		 : p_prefix.path_join(node.key);
		result.nodes.insert(name, node.value);
	}
	return OK;
}

Error WGodotResourceValidation::tag(const VP::Tag &p_tag, const StringName &p_resource_type, String &r_error) {
	current_property = String();
	assignments.clear();
	node_section = false;
	if (!types) {
		owner_type = p_tag.fields.has("type") ? StringName(String(p_tag.fields["type"])) : p_tag.name == "resource" ? p_resource_type
																													: StringName();
		resource_section = p_tag.name == "sub_resource" ? String(p_tag.fields["id"]) : String();
		node_section = p_tag.name == "node";
		if (collect_properties && (p_tag.name == "sub_resource" || p_tag.name == "resource")) {
			result.properties.insert(resource_section, HashSet<StringName>());
		}
		return OK;
	}
	RETURN_IF_ERROR(tag_fields(p_tag, r_error));
	if (p_tag.name == "ext_resource") {
		const String id = p_tag.fields["id"];
		if (external.has(id)) {
			return fail("Duplicate external resource ID: " + id, r_error);
		}
		Reference reference;
		reference.path = p_tag.fields["path"];
		if (reference.path.is_relative_path()) {
			reference.path = path.get_base_dir().path_join(reference.path).simplify_path();
		}
		RETURN_IF_ERROR(resource_type(reference.path, reference.type, r_error));
		if (!types->derives(reference.type, String(p_tag.fields["type"]))) {
			return fail("External resource type does not match its file: " + reference.path, r_error);
		}
		external.insert(id, reference);
	} else if (p_tag.name == "sub_resource") {
		if (nested_scene) {
			nested_nodes.insert(nested_scene_id, result.nodes);
			result.nodes.clear();
		}
		const String id = p_tag.fields["id"];
		if (internal.has(id)) {
			return fail("Duplicate internal resource ID: " + id, r_error);
		}
		owner_type = String(p_tag.fields["type"]);
		resource_section = id;
		RETURN_IF_ERROR(class_type(owner_type, StringName("Resource"), r_error));
		internal.insert(id, { "::" + id, owner_type });
		nested_scene = owner_type == SNAME("PackedScene");
		nested_scene_id = id;
	} else if (p_tag.name == "resource") {
		if (nested_scene) {
			nested_nodes.insert(nested_scene_id, result.nodes);
			result.nodes.clear();
			nested_scene = false;
		}
		owner_type = p_resource_type;
		resource_section = String();
	} else if (p_tag.name == "node") {
		node_section = true;
		const String parent = p_tag.fields.has("parent") ? String(p_tag.fields["parent"]) : String();
		owner_node = parent.is_empty() ? String(".") : parent == "." ? String(p_tag.fields["name"])
																	 : parent.path_join(p_tag.fields["name"]);
		const String identity = (nested_scene ? nested_scene_id : String()) + ":" + owner_node;
		if (declared_nodes.has(identity)) {
			return fail("Duplicate scene node: " + owner_node, r_error);
		}
		declared_nodes.insert(identity);
		if (p_tag.fields.has("instance")) {
			Ref<Resource> marker = p_tag.fields["instance"];
			if (marker.is_null() || !marker->has_meta(SNAME("reference"))) {
				return fail("Scene instances require an explicit resource reference.", r_error);
			}
			const String id = marker->get_meta(SNAME("reference"));
			const Reference *reference = id.begins_with("::") ? internal.getptr(id.substr(2)) : external.getptr(id);
			if (!reference) {
				return fail("Unknown scene instance.", r_error);
			}
			RETURN_IF_ERROR(instance(*reference, owner_node, r_error));
		}
		if (p_tag.fields.has("type")) {
			owner_type = String(p_tag.fields["type"]);
			RETURN_IF_ERROR(class_type(owner_type, StringName("Node"), r_error));
			result.nodes.insert(owner_node, owner_type);
		} else {
			const StringName *inherited = result.nodes.getptr(owner_node);
			if (!inherited) {
				return fail("Cannot resolve the declared type of scene node '" + owner_node + "'.", r_error);
			}
			owner_type = *inherited;
		}
		if (!parent.is_empty() && !result.nodes.has(parent)) {
			return fail("Unknown scene parent: " + parent, r_error);
		}
	} else if (p_tag.name == "connection") {
		return connection(p_tag, r_error);
	} else if (p_tag.name == "editable" && !result.nodes.has(String(p_tag.fields["path"]))) {
		return fail("Editable path does not identify a declared scene node.", r_error);
	}
	if (collect_properties && (p_tag.name == "sub_resource" || p_tag.name == "resource")) {
		result.properties.insert(resource_section, HashSet<StringName>());
	}
	return OK;
}

Error WGodotResourceValidation::tag_fields(const VP::Tag &p_tag, String &r_error) {
	for (const auto &field : p_tag.fields) {
		const String &name = field.key;
		Variant::Type expected = Variant::NIL;
		if (p_tag.name == "ext_resource") {
			if (name == "type" || name == "path" || name == "uid" || name == "id") {
				expected = Variant::STRING;
			}
		} else if (p_tag.name == "sub_resource") {
			if (name == "type" || name == "id") {
				expected = Variant::STRING;
			}
		} else if (p_tag.name == "node") {
			if (name == "name" || name == "type" || name == "parent" || name == "owner" || name == "instance_placeholder") {
				expected = Variant::STRING;
			} else if (name == "index" || name == "unique_id") {
				expected = Variant::INT;
			} else if (name == "instance") {
				expected = Variant::OBJECT;
			} else if (name == "groups" || name == "node_paths") {
				expected = Variant::ARRAY;
				if (field.value.get_type() == Variant::ARRAY) {
					for (const Variant &item : Array(field.value)) {
						if (item.get_type() != Variant::STRING && item.get_type() != Variant::STRING_NAME) {
							return fail(name + " requires literal names.", r_error);
						}
					}
				}
			}
		} else if (p_tag.name == "connection") {
			if (name == "from" || name == "to" || name == "signal" || name == "method") {
				expected = Variant::STRING;
			} else if (name == "flags" || name == "unbinds") {
				expected = Variant::INT;
			} else if (name == "binds") {
				expected = Variant::ARRAY;
			}
		} else if (p_tag.name == "editable" && name == "path") {
			expected = Variant::STRING;
		}
		if (expected == Variant::NIL || (field.value.get_type() != expected && !(name == "id" && field.value.get_type() == Variant::INT))) {
			return fail("Unknown or incorrectly typed [" + p_tag.name + "] field: " + name, r_error);
		}
	}
	return OK;
}

Error WGodotResourceValidation::literal(const Variant &p_value, const Type &p_expected, String &r_error) {
	if (!p_expected.concrete()) {
		return fail("Connection arguments require concrete declared types.", r_error);
	}
	Type actual;
	actual.builtin = p_value.get_type();
	if (actual.builtin == Variant::ARRAY && p_expected.builtin == Variant::ARRAY) {
		for (const Variant &element : Array(p_value)) {
			RETURN_IF_ERROR(literal(element, p_expected.elements[0], r_error));
		}
		return OK;
	}
	if (actual.builtin == Variant::DICTIONARY && p_expected.builtin == Variant::DICTIONARY) {
		for (const auto &entry : Dictionary(p_value)) {
			RETURN_IF_ERROR(literal(entry.key, p_expected.elements[0], r_error));
			RETURN_IF_ERROR(literal(entry.value, p_expected.elements[1], r_error));
		}
		return OK;
	}
	if (actual.builtin == Variant::OBJECT) {
		const Ref<Resource> marker = p_value;
		if (marker.is_null() || !marker->has_meta(SNAME("reference"))) {
			return fail("Connection objects require declared resource references.", r_error);
		}
		const String id = marker->get_meta(SNAME("reference"));
		const Reference *reference = id.begins_with("::") ? internal.getptr(id.substr(2)) : external.getptr(id);
		if (!reference) {
			return fail("Unknown connection resource reference.", r_error);
		}
		actual.name = reference->type;
	}
	if (p_expected.resource_path && actual.builtin == Variant::STRING) {
		StringName resource;
		RETURN_IF_ERROR(resource_type(p_value, resource, r_error));
	}
	return types->accepts(p_expected, actual) ? OK : fail("Connection argument requires " + p_expected.text() + ", got " + actual.text() + ".", r_error);
}

Error WGodotResourceValidation::connection(const VP::Tag &p_tag, String &r_error) {
	const StringName *source = result.nodes.getptr(String(p_tag.fields["from"]));
	const StringName *target = result.nodes.getptr(String(p_tag.fields["to"]));
	if (!source || !target) {
		return fail("Connection endpoints must identify declared scene nodes.", r_error);
	}
	WGodotResourceTypes::Function signal;
	WGodotResourceTypes::Function method;
	if (!types->function(*source, String(p_tag.fields["signal"]), true, signal, result.declarations) || !signal.exposed || signal.variadic ||
			!types->function(*target, String(p_tag.fields["method"]), false, method, result.declarations) || !method.exposed || method.variadic) {
		return fail("Connection requires a declared public signal and public non-variadic instance method.", r_error);
	}
	const int64_t unbinds = p_tag.fields.has("unbinds") ? int64_t(p_tag.fields["unbinds"]) : 0;
	const Array binds = p_tag.fields.has("binds") ? Array(p_tag.fields["binds"]) : Array();
	if (unbinds < 0 || unbinds > signal.parameters.size()) {
		return fail("Connection unbind count exceeds the signal arguments.", r_error);
	}
	const int passed = signal.parameters.size() - unbinds;
	const int total = passed + binds.size();
	if (total > method.parameters.size() || total < method.parameters.size() - method.defaults) {
		return fail("Connection argument count does not match its target method.", r_error);
	}
	for (int i = 0; i < passed; i++) {
		if (!signal.parameters[i].concrete() || !types->accepts(method.parameters[i], signal.parameters[i])) {
			return fail("Connection signal argument " + itos(i) + " is incompatible with its target method.", r_error);
		}
	}
	for (int i = 0; i < binds.size(); i++) {
		RETURN_IF_ERROR(literal(binds[i], method.parameters[passed + i], r_error));
	}
	return OK;
}

Error WGodotResourceValidation::finish(String &r_error) {
	current_property = String();
	if (!types) {
		return OK;
	}
	for (const NodeBinding &binding : node_bindings) {
		if (binding.target.is_empty()) {
			continue; // Godot's serialized null node reference.
		}
		const NodePath target(binding.target);
		if (target.is_absolute() || target.get_subname_count() != 0) {
			result.line = binding.line;
			return fail("Node references must resolve inside the authored scene: " + binding.target, r_error);
		}
		const String resolved = binding.owner.path_join(binding.target).simplify_path().trim_prefix("./");
		const auto *nodes = binding.scene.is_empty() || (nested_scene && binding.scene == nested_scene_id) ? &result.nodes : nested_nodes.getptr(binding.scene);
		const StringName *type = nodes ? nodes->getptr(resolved.is_empty() ? String(".") : resolved) : nullptr;
		if (!type || !types->derives(*type, binding.type)) {
			result.line = binding.line;
			return fail("Node reference '" + binding.target + "' does not resolve to " + String(binding.type) + ".", r_error);
		}
	}
	return OK;
}
