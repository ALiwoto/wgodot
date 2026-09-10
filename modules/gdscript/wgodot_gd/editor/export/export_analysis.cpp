// wgodot-changes::file

#include "export_analysis.h"

#include "export_context.h"
#include "export_transform_internal.h"
#include "obfuscation_names.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"

#include "modules/gdscript/gdscript_compiler.h"
#include "modules/gdscript/gdscript_tokenizer.h"
#include "modules/gdscript/wgodot_gd/string_obfuscation_format.h"

namespace WGodotGDScriptExportTransform {

thread_local ExportAnalysis *ExportAnalysis::active = nullptr;

ExportAnalysis::Scope::Scope(ExportAnalysis &p_analysis) :
		previous(active) {
	active = &p_analysis;
}

ExportAnalysis::Scope::~Scope() {
	active = previous;
}

ExportAnalysis::ExportAnalysis(const ExportProject &p_project, const ExportContext &p_artifacts) :
		project(p_project), artifacts(p_artifacts) {
	global_classes = project.get_external_classes();
	external_native_bases = project.get_external_native_bases();
	for (const String &path : project.get_script_paths()) {
		const String exported_path = artifacts.get_exported_script_path(path);
		if (!exported_path.is_empty()) {
			path_aliases[exported_path] = path;
		}
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(project.get_source(path)->get_text());
		for (GDScriptTokenizer::Token token = tokenizer.scan(); token.type != GDScriptTokenizer::Token::TK_EOF; token = tokenizer.scan()) {
			if (token.type == GDScriptTokenizer::Token::CLASS_NAME) {
				const GDScriptTokenizer::Token name = tokenizer.scan();
				if (name.type == GDScriptTokenizer::Token::IDENTIFIER) {
					global_classes[StringName(name.literal)] = path;
				}
				break;
			}
		}
	}
	for (const KeyValue<StringName, StringName> &entry : artifacts.get_builtin_class_aliases()) {
		native_aliases[entry.value] = entry.key;
		native_aliases[StringName(unwrap_binary_identifier_escape(entry.value))] = entry.key;
	}
	for (const KeyValue<StringName, StringName> &entry : artifacts.get_builtin_function_aliases()) {
		function_aliases[entry.value] = entry.key;
		function_aliases[StringName(unwrap_binary_identifier_escape(entry.value))] = entry.key;
	}
	const HashMap<StringName, StringName> *members[] = {
		&artifacts.get_builtin_instance_method_aliases(),
		&artifacts.get_builtin_static_method_aliases(),
		&artifacts.get_builtin_instance_property_aliases(),
		&artifacts.get_builtin_static_property_aliases(),
	};
	for (int i = 0; i < 4; i++) {
		for (const KeyValue<StringName, StringName> &entry : *members[i]) {
			const String qualified = entry.key;
			member_aliases[i][entry.value] = StringName(qualified.substr(qualified.rfind("::") + 2));
			member_aliases[i][StringName(unwrap_binary_identifier_escape(entry.value))] = StringName(qualified.substr(qualified.rfind("::") + 2));
		}
	}
}

ExportAnalysis::~ExportAnalysis() {
	// Break cycles between parser references before releasing their owners.
	for (KeyValue<String, Ref<GDScriptParserRef>> &entry : parsers) {
		entry.value->clear();
	}
	parsers.clear();
	scripts.clear();
}

const HashMap<uint64_t, String> &ExportAnalysis::get_decoded_string_resources() {
	if (!strings_decoded) {
		decoded_string_resources.reserve(artifacts.get_string_resources().size());
		for (const KeyValue<uint64_t, String> &entry : artifacts.get_string_resources()) {
			decoded_string_resources.insert(entry.key, WGodotGDScriptStringFormat::decode_fragment_text(entry.value));
		}
		strings_decoded = true;
	}
	return decoded_string_resources;
}

String ExportAnalysis::get_global_class_path(const StringName &p_name) const {
	const String *path = global_classes.getptr(p_name);
	return path != nullptr ? *path : String();
}

StringName ExportAnalysis::get_global_class_native_base(const StringName &p_name) {
	const String path = get_global_class_path(p_name);
	if (get_source(path) != nullptr) {
		Error error = OK;
		Ref<GDScriptParserRef> ref = get_parser(path, GDScriptParserRef::INHERITANCE_SOLVED, error);
		return error == OK ? ref->get_parser()->get_tree()->get_datatype().native_type : StringName();
	}
	const StringName *base = external_native_bases.getptr(path);
	return base != nullptr ? *base : StringName();
}

String ExportAnalysis::resolve_path(const String &p_path) const {
	const String path = ResourceUID::ensure_path(p_path);
	const String *original = path_aliases.getptr(path);
	return original != nullptr ? *original : path;
}

const ExportSource *ExportAnalysis::get_source(const String &p_path) const {
	return project.get_source(resolve_path(p_path));
}

Ref<GDScriptParserRef> ExportAnalysis::get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error) {
	const String path = resolve_path(p_path);
	Ref<GDScriptParserRef> ref;
	if (const Ref<GDScriptParserRef> *existing = parsers.getptr(path)) {
		ref = *existing;
	} else {
		const ExportSource *source = project.get_source(path);
		if (source == nullptr) {
			r_error = ERR_FILE_NOT_FOUND;
			return ref;
		}
		ref.instantiate();
		ref->initialize_export_source(path, source->get_text());
		parsers[path] = ref;
	}
	Scope scope(*this);
	r_error = ref->raise_status(p_status);
	return ref;
}

Ref<GDScript> ExportAnalysis::get_shallow_script(const String &p_path, Error &r_error) {
	const String path = resolve_path(p_path);
	r_error = OK;
	if (const Ref<GDScript> *existing = scripts.getptr(path)) {
		return *existing;
	}
	Ref<GDScriptParserRef> parser = get_parser(path, GDScriptParserRef::PARSED, r_error);
	if (r_error != OK) {
		return Ref<GDScript>();
	}
	Ref<GDScript> script;
	script.instantiate();
	script->set_path_cache(path);
	script->set_source_code(project.get_source(path)->get_text());
	GDScriptCompiler::make_scripts(script.ptr(), parser->get_parser()->get_tree(), false, false);
	scripts[path] = script;
	return script;
}

Error ExportAnalysis::analyze_scripts(bool p_bodies, String &r_error) {
	Scope scope(*this);
	for (const String &path : project.get_script_paths()) {
		if (!project.is_exported(path)) {
			continue;
		}
		Error error = OK;
		Ref<GDScriptParserRef> parser = get_parser(path, p_bodies ? GDScriptParserRef::FULLY_SOLVED : GDScriptParserRef::PARSED, error);
		if (error != OK) {
			r_error = "Cannot analyze export revision " + itos(project.get_revision()) + ": " + path;
			if (parser.is_valid()) {
				r_error += "\n" + get_parser_errors_with_source_text(*parser->get_parser(), project.get_source(path)->get_text());
			}
			return error;
		}
	}
	return OK;
}

StringName ExportAnalysis::resolve_native_alias(const StringName &p_name) const {
	const StringName *name = native_aliases.getptr(p_name);
	return name != nullptr ? *name : StringName();
}

StringName ExportAnalysis::resolve_function_alias(const StringName &p_name) const {
	const StringName *name = function_aliases.getptr(p_name);
	return name != nullptr ? *name : StringName();
}

StringName ExportAnalysis::resolve_member_alias(const StringName &p_name, bool p_static, bool p_property) const {
	const StringName *name = member_aliases[(p_property ? 2 : 0) + (p_static ? 1 : 0)].getptr(p_name);
	return name != nullptr ? *name : StringName();
}

} // namespace WGodotGDScriptExportTransform
