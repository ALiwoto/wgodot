// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::is_wdictionary(const Parser::DataType &p_type) const {
	return !p_type.is_coroutine && p_type.kind == Parser::DataType::BUILTIN && p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_type(0) && p_type.has_container_element_type(1);
}

bool WGodotCppEmitter::is_dictionary_duplicate(const Parser::ExpressionNode *p_value) const {
	if (p_value->type != Parser::Node::CALL) {
		return false;
	}
	const auto *call = static_cast<const Parser::CallNode *>(p_value);
	return call->function_name == SNAME("duplicate") && call->get_callee_type() == Parser::Node::SUBSCRIPT && is_wdictionary(expression_type(static_cast<const Parser::SubscriptNode *>(call->callee)->base));
}

bool WGodotCppEmitter::validate_dictionary_conversion(const Parser::ExpressionNode *p_value, const Parser::DataType &p_target, const Parser::Node *p_target_origin) {
	const auto source = expression_type(p_value);
	if (!is_wdictionary(source) && !is_wdictionary(p_target)) {
		return true;
	}
	if (p_value->type == Parser::Node::DICTIONARY && is_wdictionary(p_target)) {
		return true;
	}
	if (is_wdictionary(source) && is_wdictionary(p_target) && type(source, p_value) == type(p_target, p_target_origin ? p_target_origin : p_value)) {
		return true;
	}
	unsupported(p_value, "implicit container conversion from " + source.to_string() + " to " + p_target.to_string() + ". WDictionary sharing cannot cross a Dictionary/Variant boundary; use .duplicate() at a supported Godot API boundary");
	return false;
}

WGodotCppEmitter::Value WGodotCppEmitter::dictionary_literal(const Parser::DictionaryNode *p_dictionary, const Parser::DataType &p_target) {
	Vector<Value> operands;
	for (const auto &element : p_dictionary->elements) {
		operands.push_back(lower_converted(element.key, p_target.get_container_element_type(0), p_dictionary));
		operands.push_back(lower_converted(element.value, p_target.get_container_element_type(1), p_dictionary));
	}
	Value result = sequence(operands);
	result.cpp_type = type(p_target, p_dictionary);
	if (operands.is_empty() && !(p_dictionary->is_constant && p_dictionary->reduced && Dictionary(p_dictionary->reduced_value).is_read_only())) {
		result.code = result.cpp_type + "()";
		return result;
	}
	const String name = "temporary_" + itos(temporary_index++);
	result.setup.push_back(result.cpp_type + " " + name + ";");
	for (int i = 0; i < operands.size(); i += 2) {
		result.setup.push_back(name + ".set(" + operands[i].code + ", " + operands[i + 1].code + ");");
	}
	if (p_dictionary->is_constant && p_dictionary->reduced && Dictionary(p_dictionary->reduced_value).is_read_only()) {
		result.setup.push_back(name + ".make_read_only();");
	}
	result.code = name;
	return result;
}

String WGodotCppEmitter::dictionary_engine_argument(const Parser::ExpressionNode *p_value, Variant::Type p_target) {
	if ((p_target != Variant::DICTIONARY && p_target != Variant::NIL) || !is_dictionary_duplicate(p_value)) {
		unsupported(p_value, "passing WDictionary to a Godot API without an explicit copy. Use .duplicate() for a Dictionary/Variant argument, or add a native handler for this API");
		return String();
	}
	const auto datatype = expression_type(p_value);
	for (int i = 0; i < 2; i++) {
		if (native_only(datatype.get_container_element_type(i))) {
			unsupported(p_value, "copying native container/callback entries into a Godot Dictionary; this requires an explicit entry adapter");
			return String();
		}
	}
	const auto *call = static_cast<const Parser::CallNode *>(p_value);
	if (expression_overrides.has(call)) {
		return "(" + expression(call) + ").duplicate_to_dictionary()";
	}
	return wdictionary_call(call, true).expression();
}

WGodotCppEmitter::Value WGodotCppEmitter::wdictionary_call(const Parser::CallNode *p_call, bool p_to_dictionary) {
	const auto *base = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base;
	const auto datatype = expression_type(base);
	const auto &key_type = datatype.get_container_element_type(0);
	const auto &value_type = datatype.get_container_element_type(1);
	const StringName name = p_call->function_name;
	int key_argument = -1;
	int value_argument = -1;
	bool dictionary_argument = false;
	bool array_argument = false;
	bool import_dictionary = false;
	if (name == SNAME("get") || name == SNAME("get_or_add") || name == SNAME("set")) {
		key_argument = 0;
		value_argument = 1;
	} else if (name == SNAME("has") || name == SNAME("erase")) {
		key_argument = 0;
	} else if (name == SNAME("find_key")) {
		value_argument = 0;
	} else if (name == SNAME("assign") || name == SNAME("merge") || name == SNAME("merged")) {
		dictionary_argument = true;
	} else if (name == SNAME("has_all")) {
		array_argument = true;
	} else if (name == SNAME("sort")) {
		if (key_type.kind != Parser::DataType::ENUM && !(key_type.kind == Parser::DataType::BUILTIN && (key_type.builtin_type == Variant::INT || key_type.builtin_type == Variant::FLOAT || key_type.builtin_type == Variant::STRING))) {
			unsupported(p_call, "WDictionary.sort for this key type");
			return Value();
		}
	} else if (name != SNAME("duplicate") && name != SNAME("size") && name != SNAME("is_empty") && name != SNAME("clear") && name != SNAME("keys") && name != SNAME("values") && name != SNAME("is_read_only") && name != SNAME("make_read_only")) {
		unsupported(p_call, "WDictionary." + String(name) + "; this method needs a typed native implementation");
		return Value();
	}
	Vector<Value> operands;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		const auto *argument = p_call->arguments[i];
		if (int(i) == key_argument || int(i) == value_argument) {
			operands.push_back(lower_converted(argument, int(i) == key_argument ? key_type : value_type, base, true));
		} else if (i == 0 && name == SNAME("assign") && argument->type != Parser::Node::DICTIONARY) {
			const auto *outer_source = engine_dictionary_source;
			engine_dictionary_source = argument;
			Value source = lower(argument);
			engine_dictionary_source = outer_source;
			import_dictionary = source.cpp_type == "Variant" || source.cpp_type == "Dictionary";
			if (import_dictionary) {
				if (native_only(key_type) || native_only(value_type)) {
					unsupported(argument, "WDictionary.assign importing native container/callback entries from a Godot Dictionary; this requires an explicit entry adapter");
					return Value();
				}
			} else if (!validate_dictionary_conversion(argument, datatype, base)) {
				return Value();
			}
			operands.push_back(source);
		} else if (i == 0 && dictionary_argument) {
			operands.push_back(lower_converted(argument, datatype));
		} else if (i == 0 && array_argument) {
			Parser::DataType array;
			array.kind = Parser::DataType::BUILTIN;
			array.builtin_type = Variant::ARRAY;
			array.set_container_element_type(0, key_type);
			operands.push_back(lower_converted(argument, array));
		} else {
			operands.push_back(lower(argument));
		}
	}
	operands.push_back(lower(base));
	Value result = sequence(operands);
	if (name == SNAME("find_key") && !operands[operands.size() - 1].borrowed) {
		materialize(operands.write[operands.size() - 1], result.setup);
	}
	Vector<String> arguments;
	for (uint32_t i = 0; i < p_call->arguments.size(); i++) {
		arguments.push_back(operands[i].code);
	}
	const String receiver = "(" + operands[operands.size() - 1].code + ")";
	const String method = p_to_dictionary ? "duplicate_to_dictionary" : import_dictionary ? "assign_from_dictionary" : String(name);
	result.code = receiver + "." + method + "(" + String(", ").join(arguments) + ")";
	result.cpp_type = p_to_dictionary ? "Dictionary" : type(expression_type(p_call), p_call);
	if (name == SNAME("find_key")) {
		const String pointer = "temporary_" + itos(temporary_index++);
		result.setup.push_back("const auto *" + pointer + " = " + result.code + ";");
		result.code = pointer + " ? WGodotNative::convert<" + result.cpp_type + ">(*" + pointer + ") : " + result.cpp_type + "()";
	} else if ((name == SNAME("get") || name == SNAME("get_or_add")) && expression_type(p_call).is_variant()) {
		unsupported(p_call, "WDictionary lookup without a concrete result type; provide a typed default or destination");
		return Value();
	}
	result.effects = true;
	return result;
}

bool WGodotCppEmitter::try_lower_dictionary_get(const Parser::ExpressionNode *p_expression, Value &r_result) {
	if (p_expression->type != Parser::Node::CALL || expression_overrides.has(p_expression)) {
		return false;
	}
	const auto *call = static_cast<const Parser::CallNode *>(p_expression);
	if (call->function_name != SNAME("get") || call->get_callee_type() != Parser::Node::SUBSCRIPT) {
		return false;
	}
	const auto *base = static_cast<const Parser::SubscriptNode *>(call->callee)->base;
	const auto base_type = expression_type(base);
	if (base->type != Parser::Node::CALL || base_type.kind != Parser::DataType::BUILTIN ||
			base_type.builtin_type != Variant::DICTIONARY || is_wdictionary(base_type)) {
		return false;
	}

	// The scalar constructor consumes this lookup immediately. Keep the engine
	// dictionary and its entry as-is; do not infer a value type from the default.
	Vector<Value> operands;
	for (const auto *argument : call->arguments) {
		operands.push_back(lower_engine_argument(argument, Variant::NIL));
	}
	if (call->arguments.size() == 1) {
		operands.push_back(lower_literal(Variant(), call));
	}
	const auto *outer_source = engine_dictionary_source;
	engine_dictionary_source = base;
	operands.push_back(lower(base));
	engine_dictionary_source = outer_source;
	r_result = sequence(operands);
	r_result.code = "(" + operands[2].code + ").get(" + convert_value(operands[0], "Variant") + ", " + convert_value(operands[1], "Variant") + ")";
	r_result.cpp_type = "Variant";
	r_result.effects = true;
	return true;
}
