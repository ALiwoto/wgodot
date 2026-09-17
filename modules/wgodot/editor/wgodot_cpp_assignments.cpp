// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

#include "modules/gdscript/wgodot_gd/interface_helpers.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

StringName WGodotCppEmitter::accessor_name(const Parser::VariableNode *p_variable, bool p_setter) const {
	if (p_variable->property == Parser::VariableNode::PROP_INLINE) {
		const auto *function = p_setter ? p_variable->setter : p_variable->getter;
		return function ? function->identifier->name : StringName();
	}
	if (p_variable->property == Parser::VariableNode::PROP_SETGET) {
		const auto *identifier = p_setter ? p_variable->setter_pointer : p_variable->getter_pointer;
		return identifier ? identifier->name : StringName();
	}
	return StringName();
}

WGodotCppEmitter::Value WGodotCppEmitter::property_access(const Parser::DataType &p_base_type, const StringName &p_name, const Parser::ExpressionNode *p_origin, Value p_receiver_value, Value p_assigned) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	const bool write = !p_assigned.code.is_empty();
	Vector<Value> operands{ p_receiver_value };
	if (write) {
		operands.push_back(p_assigned);
	}
	Value result = sequence(operands);
	p_receiver_value = operands[0];
	const String p_receiver = p_receiver_value.code;
	const String p_value = write ? operands[1].code : String();
	const String p_value_type = write ? operands[1].cpp_type : String();
	result.cpp_type = write ? "void" : type(p_origin->type_constraint, p_origin);
	result.effects = true;
	auto finish = [&](const String &p_code) { result.code = p_code; return result; };
	if (const auto *contract = WGodotGDScriptInterfaceHelpers::native_interface_for_member(p_base_type, p_name)) {
		if (const auto *property = contract->properties.getptr(p_name)) {
			const StringName accessor = write ? property->setter : property->getter;
			if (accessor.is_empty()) {
				unsupported(p_origin, "missing native interface property accessor");
				return finish(String());
			}
			class_call_headers.insert(contract->cpp_header);
			const String argument = write ? convert_value(operands[1], native_argument_type(property->info, p_origin)) : "";
			if (!p_receiver_value.borrowed && !p_receiver_value.object_pointer) {
				materialize(p_receiver_value, result.setup);
			}
			const String instance = checked_receiver(result, p_receiver_value, p_receiver_value.code + ".operator->()");
			const String invoke = instance + "->" + String(accessor) + "(" + argument + ")";
			result.effects = true;
			const MethodInfo &method = contract->methods[accessor];
			return finish(write ? invoke : convert_value(native_result_value(invoke, method.return_val, method.get_argument_meta(-1), p_origin), result.cpp_type));
		}
	}
	if (p_base_type.kind == Parser::DataType::CLASS && p_base_type.class_type->wgodot_is_interface && p_base_type.class_type->has_member(p_name)) {
		return finish("(" + p_receiver + ")->" + String(write ? "set_" : "get_") + symbol(p_name) + "(" + p_value + ")");
	}
	if (p_base_type.kind == Parser::DataType::CLASS) {
		const auto *owner = member_owner(p_base_type.class_type, p_name);
		if (owner) {
			const auto entry = owner->node->get_member(p_name);
			if (entry.type != Parser::ClassNode::Member::VARIABLE) {
				unsupported(p_origin, "property " + String(p_name));
				return finish(String());
			}
			class_dependencies.insert(owner->cpp_name);
			result.cpp_type = write ? "void" : type(variable_type(entry.variable), entry.variable);
			const StringName accessor = accessor_name(entry.variable, write);
			const String receiver = entry.variable->is_static ? owner->cpp_name + "::" : p_receiver == "this" ? "this->"
																											  : "(" + p_receiver + ")->";
			const String assigned = write ? convert_value(Value(p_value, p_value_type), type(variable_type(entry.variable), entry.variable)) : String();
			// A bare property name inside its own getter/setter addresses storage.
			// Access through another receiver still invokes that receiver's accessor.
			const bool own_accessor = p_origin->type == Parser::Node::IDENTIFIER && current_function && current_function->identifier && current_function->identifier->name == accessor;
			if (!accessor.is_empty() && !own_accessor) {
				return finish(receiver + "m_" + symbol(accessor) + "(" + assigned + ")");
			}
			const String field = receiver + (entry.variable->is_static ? "static_fields()." : "") + "v_" + symbol(p_name);
			if (entry.variable->is_static && !write) {
				// GDScript loads a static variable into a temporary, unlike a direct
				// instance-field address. Preserve that snapshot and reference count.
				return finish(result.cpp_type + "(" + field + ")");
			}
			result.effects = write || !p_receiver_value.nonnull || entry.variable->is_static;
			result.borrowed = !write && p_receiver_value.nonnull && !entry.variable->is_static;
			return finish(write ? field + " = " + assigned : field);
		}
	}
	if (p_base_type.is_variant() || p_base_type.kind == Parser::DataType::BUILTIN) {
		return finish(write ? "WGodotNative::set_member(" + p_receiver + ", SNAME(" + quoted(p_name) + "), " + p_value + ")" : "WGodotNative::get_member<" + type(p_origin->type_constraint, p_origin) + ">(" + p_receiver + ", SNAME(" + quoted(p_name) + "))");
	}
	const StringName base_name = native_base(p_base_type);
	const StringName method_name = write ? ClassDB::get_property_setter(base_name, p_name) : ClassDB::get_property_getter(base_name, p_name);
	const MethodBind *method = ClassDB::get_method(base_name, method_name);
	if (!method) {
		unsupported(p_origin, "native property " + String(base_name) + "." + String(p_name));
		return finish(String());
	}
	Vector<Value> arguments;
	const int index = ClassDB::get_property_index(base_name, p_name);
	if (index >= 0) {
		arguments.push_back(Value("int64_t(" + itos(index) + ")", "int64_t"));
	}
	if (write) {
		arguments.push_back(Value(p_value, p_value_type));
	}
	Value invoked = native_invoke(method, p_receiver_value, arguments, write ? "void" : type(p_origin->type_constraint, p_origin), p_origin);
	result.setup.append_array(invoked.setup);
	invoked.setup = result.setup;
	return invoked;
}

WGodotCppEmitter::Value WGodotCppEmitter::store_identifier(const Parser::IdentifierNode *p_target, const Value &p_value) {
	switch (p_target->source) {
		case Parser::IdentifierNode::FUNCTION_PARAMETER:
		case Parser::IdentifierNode::LOCAL_VARIABLE:
		case Parser::IdentifierNode::LOCAL_ITERATOR:
		case Parser::IdentifierNode::LOCAL_BIND:
			return expression(p_target) + " = " + convert_value(p_value, value_facts(p_target, String()).cpp_type);
		default:
			return property_access(current_class->node->self_type, p_target->name, p_target, lower_receiver(nullptr), p_value);
	}
}

WGodotCppEmitter::Value WGodotCppEmitter::assignment(const Parser::AssignmentNode *p_assignment) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	const auto *target = p_assignment->assignee;
	const auto *assigned = p_assignment->assigned_value;
	const bool compound = p_assignment->operation != Parser::AssignmentNode::OP_NONE;
	const auto target_type = expression_type(target);
	if (!compound && !validate_array_conversion(assigned, target_type, target)) {
		return String();
	}
	const auto assigned_type = assigned->type == Parser::Node::ARRAY && is_warray(target_type) ? target_type : expression_type(assigned);
	const bool construct_container = (assigned->type == Parser::Node::ARRAY && is_warray(target_type)) ||
			(is_packed(target_type) && (assigned->type == Parser::Node::ARRAY || is_warray(assigned_type)));
	Value assigned_value = construct_container ? lower_converted(assigned, target_type) : lower(assigned);
	Value result;
	result.cpp_type = "void";
	if (target->type == Parser::Node::IDENTIFIER) {
		if (compound) {
			Vector<Value> operands{ assigned_value, lower(target) };
			result = sequence(operands);
			assigned_value = Value(operation(p_assignment->variant_op, target_type, target_type, assigned_type, operands[1].code, operands[0].code, p_assignment), type(target_type, target));
		} else {
			Vector<Value> operands{ assigned_value };
			result = sequence(operands);
			assigned_value = operands[0];
		}
		result.cpp_type = "void";
		result.effects = true;
		Value stored = store_identifier(static_cast<const Parser::IdentifierNode *>(target), assigned_value);
		result.setup.append_array(stored.setup);
		stored.setup = result.setup;
		stored.cpp_type = "void";
		return stored;
	}
	if (target->type != Parser::Node::SUBSCRIPT) {
		unsupported(target, "assignment target");
		return String();
	}
	const auto *subscript = static_cast<const Parser::SubscriptNode *>(target);
	const auto &base_type = subscript->base->type_constraint;
	if (!compound && subscript->is_attribute && !base_type.is_meta_type && (base_type.kind == Parser::DataType::CLASS || base_type.kind == Parser::DataType::NATIVE)) {
		// An Object property has no value-type parents to write back. Evaluate
		// its receiver before the RHS, then assign directly or call its setter.
		Vector<Value> operands{ lower_receiver(subscript->base), assigned_value };
		result = sequence(operands);
		result.cpp_type = "void";
		result.effects = true;
		Value stored = property_access(base_type, subscript->attribute->name, subscript, operands[0], operands[1]);
		result.setup.append_array(stored.setup);
		stored.setup = result.setup;
		return stored;
	}
	Vector<const Parser::SubscriptNode *> chain;
	const Parser::ExpressionNode *root = target;
	while (root->type == Parser::Node::SUBSCRIPT) {
		const auto *node = static_cast<const Parser::SubscriptNode *>(root);
		chain.insert(0, node);
		root = node->base;
	}
	const String prefix = "assignment_" + itos(temporary_index++) + "_";
	String body;
	// Borrow the root slot, but own intermediate get results as the language does.
	// Read the RHS before the final index, and only then read a compound target.
	if (!root->type_constraint.is_meta_type) {
		Value root_value = lower(root);
		Vector<Value> roots{ root_value };
		result.setup.append_array(sequence(roots).setup);
		root_value = roots[0];
		body += "auto &&" + prefix + "base_0 = " + root_value.code + ";\n";
	}
	auto key = [&](int p_index) { return prefix + "key_" + itos(p_index); };
	auto base = [&](int p_index) { return prefix + "base_" + itos(p_index); };
	auto read = [&](int p_index) {
		const auto *node = chain[p_index];
		if (p_index + 1 < chain.size()) {
			if (const String *saved = expression_overrides.getptr(node)) {
				return *saved;
			}
		}
		return node->is_attribute ? property_access(node->base->type_constraint, node->attribute->name, node, Value(base(p_index), type(node->base->type_constraint, node))).expression() : "WGodotNative::get_index<" + type(node->type_constraint, node) + ">(" + base(p_index) + ", " + key(p_index) + ")";
	};
	auto write = [&](int p_index, const String &p_value) {
		const auto *node = chain[p_index];
		// These bases are already evaluated local slots. In particular, a
		// packed vector/color element must be mutated before it is written back.
		Value receiver(base(p_index), type(node->base->type_constraint, node));
		receiver.borrowed = true;
		receiver.effects = false;
		return node->is_attribute ? property_access(node->base->type_constraint, node->attribute->name, node, receiver, Value(p_value, type(node->type_constraint, node))).expression() : "WGodotNative::set_index(" + base(p_index) + ", " + key(p_index) + ", " + p_value + ")";
	};
	for (int i = 0; i < chain.size(); i++) {
		if (i == chain.size() - 1) {
			Vector<Value> values{ assigned_value };
			const Value prepared = sequence(values);
			assigned_value = values[0];
			for (const String &step : prepared.setup) {
				body += step + "\n";
			}
			body += "auto &&" + prefix + "value = " + assigned_value.code + ";\n";
		}
		if (!chain[i]->is_attribute) {
			body += "auto &&" + key(i) + " = " + expression(chain[i]->index) + "; ";
		}
		if (i < chain.size() - 1) {
			body += String(expression_overrides.has(chain[i]) ? "auto &&" : "auto ") + base(i + 1) + " = " + read(i) + "; ";
		}
	}
	const int leaf = chain.size() - 1;
	if (compound) {
		body += "auto " + prefix + "previous = " + read(leaf) + ";\nauto " + prefix + "result = " + operation(p_assignment->variant_op, target_type, target_type, assigned_type, prefix + "previous", prefix + "value", p_assignment) + ";\n";
	}
	body += write(leaf, prefix + (compound ? "result" : "value")) + ";\n";
	auto write_back = [&](const Parser::DataType &p_type, const String &p_value, const String &p_write) {
		if (p_type.is_variant()) {
			return "if (!" + p_value + ".is_shared()) { " + p_write + "; } ";
		}
		return p_type.kind == Parser::DataType::BUILTIN && !Variant::is_type_shared(p_type.builtin_type) ? p_write + "; " : String();
	};
	for (int i = leaf - 1; i >= 0; i--) {
		const auto &datatype = chain[i]->type_constraint;
		// Do not even emit a setter for a known shared value: it may be read-only.
		if (datatype.is_variant() || (datatype.kind == Parser::DataType::BUILTIN && !Variant::is_type_shared(datatype.builtin_type))) {
			body += write_back(datatype, base(i + 1), write(i, base(i + 1)));
		}
	}
	if (root->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(root);
		const bool field = identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE || identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE || identifier->source == Parser::IdentifierNode::STATIC_VARIABLE;
		const auto &datatype = root->type_constraint;
		if (field && (datatype.is_variant() || (datatype.kind == Parser::DataType::BUILTIN && !Variant::is_type_shared(datatype.builtin_type)))) {
			body += write_back(datatype, base(0), store_identifier(identifier, Value(base(0), type(datatype, root))).expression());
		}
	}
	result.setup.push_back(body);
	return result;
}
