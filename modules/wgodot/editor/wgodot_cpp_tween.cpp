// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/wgodot_gd/editor/property_path.h"

using Parser = GDScriptParser;

WGodotCppEmitter::Value WGodotCppEmitter::tween_property_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base) {
	const GDScriptAnalyzer *analyzer = project.find_analyzer(current_class->script_path);
	const WGodotGDScriptPropertyPath *path = analyzer ? analyzer->wgodot_get_tween_property_path(p_call) : nullptr;
	if (!path || path->segments.is_empty() || path->segments.size() != path->path.get_as_property_path().get_subname_count()) {
		unsupported(p_call, "Tween.tween_property requires a fully analyzer-resolved constant property path");
		return String();
	}
	const auto target_type = expression_type(p_call->arguments[0]);
	const bool target_interface = is_interface_type(target_type);
	const auto &value_type = path->segments[path->segments.size() - 1].datatype;
	if (native_only(value_type)) {
		unsupported(p_call, "tweening a native container or callback value through Godot's Variant interpolation");
		return String();
	}
	const String target = class_name(target_type, p_call);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_tween.h");

	// Match native_call's argument/receiver evaluation order, but never emit
	// the path literal: only its analyzer-resolved property accesses survive.
	Vector<Value> operands{ target_interface ? lower_converted(p_call->arguments[0], target_type) : lower(p_call->arguments[0]), lower(p_call->arguments[2]), lower(p_call->arguments[3]), lower_receiver(p_base) };
	Value result = sequence(operands);
	result.cpp_type = "Ref<PropertyTweener>";
	result.effects = true;
	const String receiver = target_interface ? operands[0].code : "static_cast<" + target + " *>(WGodotNative::object_pointer(" + operands[0].code + "))";
	result.code = "WGodotNative::tween_property(WGodotNative::object_pointer(" + operands[3].code + "), " + receiver + ",\n";
	result.code += property_path_accessor(*path, p_call, false) + ",\n" + property_path_accessor(*path, p_call, true) + ", " + convert_value(operands[1], "Variant") + ", " + operands[2].code + ")";
	return result;
}
