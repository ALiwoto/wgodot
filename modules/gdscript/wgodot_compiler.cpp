// wgodot-changes::file

#include "gdscript_compiler.h"
#include "gdscript.h"
#include "gdscript_cache.h"
#include "wgodot_gd/interface_helpers.h"
#include "wgodot_stdlib.h"

bool GDScriptCompiler::_wgodot_compile_interface_identifier(CodeGen &p_codegen, const StringName &p_name, const GDScriptParser::ExpressionNode *p_source, GDScriptCodeGenerator::Address &r_address, Error &r_error) {
	if (!WGodotGDScriptStdLib::has_global_interface(p_name)) {
		return false;
	}
	Ref<GDScript> contract = GDScriptCache::get_shallow_script(WGodotGDScriptStdLib::get_global_interface_path(p_name), r_error);
	if (r_error != OK) {
		_set_error(vformat("Cannot load built-in interface '%s'.", p_name), p_source);
		return true;
	}
	r_address = p_codegen.add_constant(contract);
	return true;
}

String GDScriptCompiler::_wgodot_make_interface_key(const String &p_script_path, const String &p_fqcn) {
	if (p_script_path.is_empty() || p_fqcn.is_empty()) {
		return String();
	}

	return GDScript::canonicalize_path(p_script_path) + "::" + p_fqcn;
}

String GDScriptCompiler::_wgodot_get_interface_key_from_datatype(const GDScriptParser::DataType &p_datatype) {
	if (p_datatype.kind != GDScriptParser::DataType::CLASS || p_datatype.class_type == nullptr || !p_datatype.class_type->wgodot_is_interface) {
		return String();
	}

	return _wgodot_make_interface_key(p_datatype.script_path, p_datatype.class_type->fqcn);
}

void GDScriptCompiler::_wgodot_prepare_interface_metadata(GDScript *p_script, const GDScriptParser::ClassNode *p_class) {
	ERR_FAIL_NULL(p_script);
	ERR_FAIL_NULL(p_class);

	p_script->wgodot_is_interface = p_class->wgodot_is_interface;
	p_script->wgodot_interface_key = p_class->wgodot_is_interface ? _wgodot_make_interface_key(p_script->path, p_class->fqcn) : String();
	p_script->wgodot_implemented_interfaces.clear();

	for (const GDScriptParser::ClassNode *contract : p_class->wgodot_resolved_interfaces) {
		const String interface_key = WGodotGDScriptInterfaceHelpers::get_interface_id(contract);
		if (!interface_key.is_empty()) {
			p_script->wgodot_implemented_interfaces.insert(interface_key);
		}
	}
}
