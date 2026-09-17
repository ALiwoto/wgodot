// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

using Parser = GDScriptParser;

WGodotCppEmitter::Value WGodotCppEmitter::javascript_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const Parser::DataType &p_base_type) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_javascript.h");
	Vector<Value> operands;
	// GDScript evaluates the method name and arguments before the receiver.
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		operands.push_back(lower_engine_argument(p_call->arguments[i], i == 0 ? Variant::STRING_NAME : Variant::NIL));
	}
	operands.push_back(lower_receiver(p_base));
	Value result = sequence(operands);
	Value receiver = operands[operands.size() - 1];
	if (!receiver.borrowed && !receiver.object_pointer) {
		materialize(receiver, result.setup);
	}
	const String instance = checked_receiver(result, receiver, receiver_pointer(receiver, p_base_type, p_call));
	Vector<String> arguments{ instance, convert_value(operands[0], "StringName") };
	for (uint32_t i = 1; i < p_call->arguments.size(); i++) {
		arguments.push_back(operands[i].code);
	}
	result.code = "WGodotNative::javascript_call(" + String(", ").join(arguments) + ")";
	result.cpp_type = "Variant";
	result.effects = true;
	return result;
}
