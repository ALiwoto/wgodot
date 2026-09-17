// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

using Parser = GDScriptParser;

WGodotCppEmitter::Value WGodotCppEmitter::group_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base, const Parser::DataType &p_base_type) {
	class_call_headers.insert("scene/main/wgodot_scene_tree.h");
	Parser::DataType target = p_call->arguments[0]->type_constraint;
	target.is_meta_type = false;
	const StringName name = p_call->arguments[1]->reduced_value;
	const String target_class = class_name(target, p_call);
	const auto *owner = target.kind == Parser::DataType::CLASS ? member_owner(target.class_type, name) : nullptr;
	const auto *script_method = owner && owner->node->get_member(name).type == Parser::ClassNode::Member::FUNCTION ? owner->node->get_member(name).function : nullptr;
	const MethodBind *native_method = script_method ? nullptr : ClassDB::get_method(native_base(target), name);
	if (!script_method && !native_method) {
		unsupported(p_call, "call_group_as() target " + target.to_string() + "." + String(name));
		return Value();
	}
	Value result;
	Vector<Value> arguments;
	// Freeze each supplied argument before evaluating the tree receiver. In
	// particular, .duplicate() and engine boundary conversions happen once.
	for (uint32_t i = 2; i < p_call->arguments.size(); i++) {
		const auto *argument = p_call->arguments[i];
		Value value = script_method ? lower_converted(argument, script_method->parameters[i - 2]->type_constraint, script_method->parameters[i - 2], true) : lower_engine_argument(argument, native_method->get_argument_type(i - 2));
		value.borrowed = false;
		materialize(value, result.setup);
		arguments.push_back(value);
	}
	Value tree = lower_receiver(p_base);
	tree.borrowed = false;
	materialize(tree, result.setup);
	const String instance = materialize_receiver(result, tree, receiver_pointer(tree, p_base_type, p_call), true);
	const String node_name = "group_node_" + itos(temporary_index++);
	Value node("static_cast<" + target_class + " *>(" + node_name + ")", target_class + " *");
	node.object_pointer = node.nonnull = node.invariant = true;
	node.effects = false;
	Value invocation;
	if (script_method) {
		class_dependencies.insert(owner->cpp_name);
		Vector<String> values;
		for (const Value &argument : arguments) {
			values.push_back(argument.code);
		}
		invocation.code = node.code + "->m_" + WGodotCppNames::symbol(name) + "(" + String(", ").join(values) + ")";
		invocation.cpp_type = "void";
	} else {
		invocation = native_invoke(native_method, node, arguments, "void", p_call);
	}
	result.code = "WGodotSceneTree::call_group_as(" + instance + ", [](Node *node) { return Object::cast_to<" + target_class + ">(node) != nullptr; }, [&](Node *" + node_name + ") {\n" + invocation.statement(1) + "})";
	result.cpp_type = "void";
	result.effects = true;
	return result;
}
