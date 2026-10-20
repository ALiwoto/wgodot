// wgodot-changes::file
#include "wgodot_named_types.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/variant/variant_parser.h"

Ref<Script> WGodotNamedTypes::script(const StringName &p_name) {
	if (!ScriptServer::is_global_class(p_name)) {
		return Ref<Script>();
	}
	Ref<Script> result = ResourceLoader::load(ScriptServer::get_global_class_path(p_name), "Script");
	ERR_FAIL_COND_V_MSG(result.is_null() || !result->is_script_valid() || result->get_global_name() != p_name,
			Ref<Script>(), "Cannot resolve class_name '" + String(p_name) + "'.");
	return result;
}

StringName WGodotNamedTypes::native_type(const StringName &p_name) {
	return ScriptServer::is_global_class(p_name) ? ScriptServer::get_global_class_native_base(p_name) : p_name;
}

StringName WGodotNamedTypes::type_name(const Object *p_object) {
	const Ref<Script> attached = p_object->get_script();
	return attached.is_valid() ? attached->get_global_name() : p_object->get_class_name();
}

Ref<Script> WGodotNamedTypes::Resolver::script(const StringName &p_name) {
	if (const Ref<Script> *cached = scripts.getptr(p_name)) {
		return *cached;
	}
	Ref<Script> result = WGodotNamedTypes::script(p_name);
	if (result.is_valid()) {
		scripts.insert(p_name, result);
	}
	return result;
}

Error WGodotNamedTypes::Resolver::initialize_resource(const Ref<Resource> &p_resource, const StringName &p_name) {
	if (!ScriptServer::is_global_class(p_name)) {
		return OK;
	}
	const Ref<Script> definition = script(p_name);
	ERR_FAIL_COND_V_MSG(definition.is_null() || definition->is_abstract() || definition->wgodot_is_interface_type(), ERR_CANT_CREATE,
			"Cannot instantiate class_name '" + String(p_name) + "'.");
	// The loader must own the resource before script initialization. The VM's
	// temporary self reference must not become its only reference and release it.
	p_resource->set_script(definition);
	const Ref<Script> attached = p_resource->get_script();
	ERR_FAIL_COND_V_MSG(attached != definition, ERR_CANT_CREATE,
			"Failed to initialize class_name '" + String(p_name) + "'.");
	return OK;
}

bool WGodotNamedTypes::is_script_reference(const String &p_type, const String &p_path) {
	return ClassDB::is_parent_class(p_type, SNAME("Script")) || p_path.get_extension() == "gd" || p_path.get_extension() == "gdc";
}

void WGodotNamedTypes::dependencies(const Ref<FileAccess> &p_file, List<String> *r_dependencies, bool p_add_types) {
	// Scan type declarations without decoding animation/mesh payloads or loading scripts.
	// StreamFile buffers input, so this also works on very large text resources.
	p_file->seek(0);
	VariantParser::StreamFile stream;
	stream.f = p_file;
	HashSet<StringName> classes;
	bool tag_candidate = false;
	bool type_tag = false;
	bool container = false;
	bool container_pending = false;
	bool type_pending = false;
	char32_t ch = stream.get_char();
	while (ch) {
		if (ch == ';' || ch == '#') {
			while (ch && ch != '\n') {
				ch = stream.get_char();
			}
			continue;
		}
		if (ch == '"') {
			String text;
			ch = stream.get_char();
			while (ch && ch != '"') {
				if (ch == '\\') {
					ch = stream.get_char();
				}
				if (type_pending) {
					text += ch;
				}
				ch = stream.get_char();
			}
			if (type_pending && ScriptServer::is_global_class(text)) {
				classes.insert(text);
			}
			type_pending = false;
			ch = stream.get_char();
			continue;
		}
		if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
			do {
				ch = stream.get_char();
			} while ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E');
			continue;
		}
		if (ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch >= 128) {
			String word;
			do {
				word += ch;
				ch = stream.get_char();
			} while (ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch >= 128);
			if (tag_candidate) {
				type_tag = word == "node" || word == "sub_resource" || word == "gd_resource" || word == "ext_resource";
				tag_candidate = false;
			}
			if (container && ScriptServer::is_global_class(word)) {
				classes.insert(word);
			}
			type_pending = type_tag && word == "type";
			container_pending = word == "Array" || word == "Dictionary";
			continue;
		}
		if (ch == '[') {
			container = container_pending;
			tag_candidate = !container;
			container_pending = false;
		} else if (ch == ']') {
			container = false;
			type_tag = false;
			tag_candidate = false;
		}
		ch = stream.get_char();
	}
	for (const StringName &name : classes) {
		String path = ScriptServer::get_global_class_path(name);
		if (p_add_types) {
			path += "::" + String(ScriptServer::get_global_class_language(name));
		}
		r_dependencies->push_back(path);
	}
}
