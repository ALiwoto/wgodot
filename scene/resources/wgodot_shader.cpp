// wgodot-changes::file
#include "shader.h"

#include "servers/rendering/rendering_server.h"

Ref<WShaderParameterLayout> Shader::get_parameter_layout() const {
	_update_shader();
	_check_shader_rid();
	MutexLock lock(shader_rid_mutex);
	if (parameter_layout.is_null() && shader_rid.is_valid()) {
		parameter_layout = RS::get_singleton()->shader_get_parameter_layout(shader_rid);
	}
	return parameter_layout;
}

uint64_t Shader::get_parameter_revision() const {
	_update_shader();
	return parameter_revision;
}
