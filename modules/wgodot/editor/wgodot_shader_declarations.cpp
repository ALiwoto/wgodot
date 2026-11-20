// wgodot-changes::file
#include "wgodot_shader_declarations.h"

#include "core/io/file_access.h"
#include "core/io/wgodot_file_stamp.h"
#include "scene/resources/shader_include.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"
#include "servers/rendering/rendering_server.h"

void WGodotShaderDeclarations::parse(const String &p_path, Entry &r_entry) {
	r_entry.dependencies.insert(p_path, WGodotFileStamp::get(p_path));
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &r_entry.error);
	if (file.is_null()) {
		r_entry.message = "Cannot read shader declarations: " + p_path;
		return;
	}
	if (p_path.get_extension() != "gdshader") {
		r_entry.error = ERR_UNAVAILABLE;
		r_entry.message = "Shader parameter checking requires a .gdshader source: " + p_path;
		return;
	}
	String code;
	HashSet<Ref<ShaderInclude>> includes;
	ShaderPreprocessor preprocessor;
	r_entry.error = preprocessor.preprocess(file->get_as_text(), p_path, code, &includes);
	for (const Ref<ShaderInclude> &include : includes) {
		r_entry.dependencies.insert(include->get_path(), WGodotFileStamp::get(include->get_path()));
	}
	if (r_entry.error != OK) {
		r_entry.message = "Cannot preprocess shader: " + p_path;
		return;
	}
	const String kind = ShaderLanguage::get_shader_type(code);
	const String names[] = { "spatial", "canvas_item", "particles", "sky", "fog", "texture_blit" };
	RSE::ShaderMode mode = RSE::SHADER_MAX;
	for (int i = 0; i < RSE::SHADER_MAX; i++) {
		if (kind == names[i]) {
			mode = RSE::ShaderMode(i);
			break;
		}
	}
	if (mode == RSE::SHADER_MAX) {
		r_entry.error = ERR_PARSE_ERROR;
		r_entry.message = "Unknown shader type in " + p_path;
		return;
	}
	ShaderTypes *types = ShaderTypes::get_singleton();
	ShaderLanguage::ShaderCompileInfo info;
	info.functions = types->get_functions(mode);
	info.render_modes = types->get_modes(mode);
	info.stencil_modes = types->get_stencil_modes(mode);
	info.shader_types = types->get_types();
	info.global_shader_uniform_type_func = [](const StringName &p_name) {
		return ShaderLanguage::DataType(RenderingServer::global_shader_uniform_type_get_shader_datatype(
				RenderingServer::get_singleton()->global_shader_parameter_get_type(p_name)));
	};
	ShaderLanguage language;
	r_entry.error = language.compile(code, info);
	if (r_entry.error != OK) {
		r_entry.message = vformat("%s:%d: %s", p_path, language.get_error_line(), language.get_error_text());
		return;
	}
	for (const auto &uniform : language.get_shader()->uniforms) {
		if (uniform.value.scope == ShaderLanguage::ShaderNode::Uniform::SCOPE_LOCAL) {
			r_entry.uniforms.insert(uniform.key, ShaderLanguage::uniform_to_property_info(uniform.value));
		} else if (uniform.value.scope == ShaderLanguage::ShaderNode::Uniform::SCOPE_INSTANCE) {
			r_entry.instance_uniforms.insert(uniform.key, ShaderLanguage::uniform_to_property_info(uniform.value));
		}
	}
}

Error WGodotShaderDeclarations::uniform(const String &p_path, const StringName &p_name, PropertyInfo &r_info, HashMap<String, String> &r_dependencies, String &r_error, bool p_instance) const {
	std::shared_ptr<Entry> entry;
	{
		MutexLock lock(mutex);
		auto &cached = entries[p_path];
		if (!cached) {
			cached = std::make_shared<Entry>();
		}
		entry = cached;
	}
	MutexLock lock(entry->mutex);
	if (!entry->ready) {
		parse(p_path, *entry);
		entry->ready = true;
	}
	for (const auto &dependency : entry->dependencies) {
		r_dependencies.insert(dependency.key, dependency.value);
	}
	if (entry->error != OK) {
		r_error = entry->message;
		return entry->error;
	}
	const PropertyInfo *info = (p_instance ? entry->instance_uniforms : entry->uniforms).getptr(p_name);
	if (!info) {
		r_error = "No " + String(p_instance ? "instance" : "material") + " uniform '" + String(p_name) + "' in " + p_path;
		return ERR_DOES_NOT_EXIST;
	}
	r_info = *info;
	return OK;
}
