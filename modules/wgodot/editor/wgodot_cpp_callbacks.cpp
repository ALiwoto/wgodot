// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "modules/gdscript/gdscript_analyzer.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::validate_callback(const Parser::ExpressionNode *p_source, const WGodotCppSignatures::Signature &p_target, bool p_discard_result) {
	const auto *source = signatures.get(p_source);
	if (!source) {
		(void)signature_type(p_source);
		return false;
	}
	const int count = p_target.arguments.size();
	if (count > source->arguments.size() || count < source->arguments.size() - source->defaults) {
		unsupported(p_source, "callback assignment/connection with incompatible argument counts");
		return false;
	}
	for (int i = 0; i < count; i++) {
		const auto &parameter = source->arguments[i];
		const auto &argument = p_target.arguments[i];
		const bool compatible = !parameter.type.is_variant() && !argument.type.is_variant() && GDScriptAnalyzer::check_type_compatibility(parameter.type, argument.type, true);
		if (!compatible || ((is_warray(parameter.type) || is_warray(argument.type) || is_wdictionary(parameter.type) || is_wdictionary(argument.type)) && type(parameter.type, parameter.origin) != type(argument.type, argument.origin))) {
			unsupported(p_source, "callback argument " + itos(i + 1) + " cannot accept " + argument.type.to_string() + " as " + parameter.type.to_string());
			return false;
		}
	}
	const auto &result = p_target.result.type;
	if (!p_discard_result && !(result.kind == Parser::DataType::BUILTIN && result.builtin_type == Variant::NIL && !result.is_coroutine)) {
		if (result.is_coroutine != source->result.type.is_coroutine || !GDScriptAnalyzer::check_type_compatibility(result, source->result.type, true)) {
			unsupported(p_source, "callback assignment with incompatible result types");
			return false;
		}
	}
	return true;
}

bool WGodotCppEmitter::native_only(const Parser::DataType &p_type) const {
	return p_type.is_coroutine || is_warray(p_type) || is_wdictionary(p_type) || WGodotCppSignatures::contains_signature(p_type);
}

String WGodotCppEmitter::function_result(const Parser::FunctionNode *p_function) {
	auto result = p_function->return_type_constraint;
	result.is_coroutine = p_function->is_coroutine;
	return type(result, p_function);
}

String WGodotCppEmitter::signature_type(const Parser::Node *p_origin, bool p_signal) {
	if (rendering_signatures.has(p_origin)) {
		unsupported(p_origin, "a recursively defined callback signature without a finite native type");
		return String();
	}
	struct Rendering {
		HashSet<const Parser::Node *> &active;
		const Parser::Node *origin;
		~Rendering() { active.erase(origin); }
	} rendering{ rendering_signatures, p_origin };
	rendering_signatures.insert(p_origin);
	const auto *signature = signatures.get(p_origin);
	if (!signature) {
		if (p_origin->type == Parser::Node::CALL) {
			const auto *call = static_cast<const Parser::CallNode *>(p_origin);
			if (call->function_name == SNAME("unbind") && call->get_callee_type() == Parser::Node::SUBSCRIPT &&
					expression_type(static_cast<const Parser::SubscriptNode *>(call->callee)->base).builtin_type == Variant::CALLABLE) {
				unsupported(p_origin, "native unbind() without a positive constant count and a concrete connection, invocation, or assignment signature");
				return String();
			}
		}
		unsupported(p_origin, "a concrete native callback/signal signature at this use; no typed declaration, lambda, method reference, or assignment resolves it");
		return String();
	}
	Vector<String> arguments;
	for (const auto &argument : signature->arguments) {
		if (argument.type.is_variant()) {
			unsupported(p_origin, "a native callback/signal with a Variant argument");
			return String();
		}
		arguments.push_back(type(argument.type, argument.origin));
	}
	if (p_signal) {
		class_native_headers.insert("modules/wgodot/native/wgodot_native_signal.h");
		return "WGodotNative::WSignal<" + String(", ").join(arguments) + ">";
	}
	if (signature->result.type.is_variant()) {
		unsupported(p_origin, "a native callback with a Variant result");
		return String();
	}
	class_native_headers.insert("modules/wgodot/native/wgodot_native_callback.h");
	return "WGodotNative::WCallable<" + type(signature->result.type, signature->result.origin) + "(" + String(", ").join(arguments) + ")>";
}

WGodotCppEmitter::Value WGodotCppEmitter::callback_call(const Parser::CallNode *p_call) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	const bool signal = expression_type(base).builtin_type == Variant::SIGNAL;
	const StringName name = p_call->function_name;
	const auto *signature = signatures.get(base);
	if (!signature) {
		(void)signature_type(base, signal);
		return String();
	}
	if (!signal && name == SNAME("unbind")) {
		const auto *output = signatures.get(p_call);
		if (!output) {
			(void)signature_type(p_call);
			return String();
		}
		const auto *count = p_call->arguments[0];
		if (!count->is_constant || !count->reduced || count->reduced_value.get_type() != Variant::INT || int64_t(count->reduced_value) <= 0 || int64_t(count->reduced_value) > output->arguments.size()) {
			unsupported(p_call, "native unbind() without a positive constant count within the supplied argument count");
			return String();
		}
		const int64_t ignored = count->reduced_value;
		auto forwarded = *output;
		forwarded.arguments.resize(forwarded.arguments.size() - ignored);
		if (!validate_callback(base, forwarded)) {
			return String();
		}
		Value result = lower(base);
		result.cpp_type = signature_type(p_call);
		result.code = result.cpp_type + "::unbind<" + itos(ignored) + ">(" + result.code + ")";
		result.effects = true;
		result.borrowed = false;
		return result;
	}
	const bool invocation = name == SNAME("call") || name == SNAME("call_deferred") || name == SNAME("emit");
	const bool binding = !signal && name == SNAME("bind");
	if (binding && int(p_call->arguments.size()) > signature->arguments.size()) {
		unsupported(p_call, "binding more arguments than the native callback accepts");
		return String();
	}
	if (signal && name == SNAME("connect") && !p_call->arguments.is_empty()) {
		if (!validate_callback(p_call->arguments[0], *signature, true)) {
			return String();
		}
	}
	if (!invocation && !binding && name != SNAME("connect") && name != SNAME("disconnect") && name != SNAME("is_connected") && name != SNAME("is_valid") && name != SNAME("is_null") && name != SNAME("get_object_id") && name != SNAME("get_argument_count")) {
		unsupported(p_call, "native callback/signal operation " + String(name));
		return String();
	}
	if (invocation && (int(p_call->arguments.size()) > signature->arguments.size() || int(p_call->arguments.size()) < signature->arguments.size() - signature->defaults)) {
		unsupported(p_call, "native callback invocation with an incompatible argument count");
		return String();
	}
	Vector<Value> operands;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const auto *argument = p_call->arguments[i];
		const int parameter_index = binding ? signature->arguments.size() - p_call->arguments.size() + i : i;
		Value value;
		if ((invocation || binding) && parameter_index >= 0 && parameter_index < signature->arguments.size()) {
			const auto &parameter = signature->arguments[parameter_index];
			// bind/deferred store a deduced tuple, so those values must already
			// have their owning parameter type before they leave this expression.
			value = lower_converted(argument, parameter.type, parameter.origin, name == SNAME("emit") || name == SNAME("call"));
		} else {
			value = lower(argument);
		}
		operands.push_back(value);
	}
	operands.push_back(lower(base));
	Value result = sequence(operands);
	Vector<String> arguments;
	for (int i = 0; i < operands.size() - 1; i++) {
		arguments.push_back(operands[i].code);
	}
	result.code = operands[operands.size() - 1].code + "." + String(name) + "(" + String(", ").join(arguments) + ")";
	result.effects = true;
	return result;
}
