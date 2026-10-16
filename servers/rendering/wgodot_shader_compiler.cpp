// wgodot-changes::file
#include "shader_compiler.h"

Error ShaderCompiler::compile_concurrent(RSE::ShaderMode p_mode, const String &p_code, IdentifierActions *p_actions, const String &p_path, GeneratedCode &r_gen_code) const {
	// Do not hold a renderer lock while parsing a background-loaded shader.
	// The live renderer needs those locks for unrelated material updates and
	// pipeline publication. Work-local compilers also allow independent loads
	// to use the existing worker pool concurrently, without another scheduler.
	ShaderCompiler compiler;
	compiler.initialize(actions);
	return compiler.compile(p_mode, p_code, p_actions, p_path, r_gen_code);
}
