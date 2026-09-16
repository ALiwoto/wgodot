// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"

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

String WGodotCppEmitter::property_access(const Parser::DataType &p_base_type, const StringName &p_name, const Parser::ExpressionNode *p_origin, const String &p_receiver, const String &p_value) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	const bool write = !p_value.is_empty();
	if (p_base_type.kind == Parser::DataType::CLASS && p_base_type.class_type->wgodot_is_interface && p_base_type.class_type->has_member(p_name)) {
		if (is_warray(p_origin->type_constraint)) {
			unsupported(p_origin, "WArray interface property through the current Variant property ABI");
			return String();
		}
		return write ? "WGodotNative::set_member(" + p_receiver + ", SNAME(" + quoted(p_name) + "), " + p_value + ")" : "WGodotNative::get_member<" + type(p_origin->type_constraint, p_origin) + ">(" + p_receiver + ", SNAME(" + quoted(p_name) + "))";
	}
	if (p_base_type.kind == Parser::DataType::CLASS) {
		const auto *owner = member_owner(p_base_type.class_type, p_name);
		if (owner) {
			const auto entry = owner->node->get_member(p_name);
			if (entry.type != Parser::ClassNode::Member::VARIABLE) {
				unsupported(p_origin, "property " + String(p_name));
				return String();
			}
			class_dependencies.insert(owner->cpp_name);
			const StringName accessor = accessor_name(entry.variable, write);
			const String receiver = entry.variable->is_static ? owner->cpp_name + "::" : "(" + p_receiver + ")->";
			// A bare property name inside its own getter/setter addresses storage.
			// Access through another receiver still invokes that receiver's accessor.
			const bool own_accessor = p_origin->type == Parser::Node::IDENTIFIER && current_function && current_function->identifier && current_function->identifier->name == accessor;
			if (!accessor.is_empty() && !own_accessor) {
				return receiver + "m_" + symbol(accessor) + "(" + (write ? "WGodotNative::convert<" + type(entry.variable->type_constraint, entry.variable) + ">(" + p_value + ")" : "") + ")";
			}
			const String field = receiver + (entry.variable->is_static ? "static_fields()." : "") + "v_" + symbol(p_name);
			if (entry.variable->is_static && !write) {
				// GDScript loads a static variable into a temporary, unlike a direct
				// instance-field address. Preserve that snapshot and reference count.
				return "([&]() { return " + field + "; }())";
			}
			return write ? field + " = WGodotNative::convert<" + type(entry.variable->type_constraint, entry.variable) + ">(" + p_value + ")" : field;
		}
	}
	if (p_base_type.is_variant() || p_base_type.kind == Parser::DataType::BUILTIN) {
		return write ? "WGodotNative::set_member(" + p_receiver + ", SNAME(" + quoted(p_name) + "), " + p_value + ")" : "WGodotNative::get_member<" + type(p_origin->type_constraint, p_origin) + ">(" + p_receiver + ", SNAME(" + quoted(p_name) + "))";
	}
	const StringName base_name = native_base(p_base_type);
	const StringName method_name = write ? ClassDB::get_property_setter(base_name, p_name) : ClassDB::get_property_getter(base_name, p_name);
	const MethodBind *method = ClassDB::get_method(base_name, method_name);
	if (!method) {
		unsupported(p_origin, "native property " + String(base_name) + "." + String(p_name));
		return String();
	}
	Vector<String> arguments;
	const int index = ClassDB::get_property_index(base_name, p_name);
	if (index >= 0) {
		arguments.push_back("int64_t(" + itos(index) + ")");
	}
	if (write) {
		arguments.push_back(p_value);
	}
	return native_invoke(method, "WGodotNative::object_pointer(" + p_receiver + ")", arguments, write ? "void" : type(p_origin->type_constraint, p_origin), p_origin);
}

String WGodotCppEmitter::store_identifier(const Parser::IdentifierNode *p_target, const String &p_value) {
	switch (p_target->source) {
		case Parser::IdentifierNode::FUNCTION_PARAMETER:
		case Parser::IdentifierNode::LOCAL_VARIABLE:
		case Parser::IdentifierNode::LOCAL_ITERATOR:
		case Parser::IdentifierNode::LOCAL_BIND:
			return expression(p_target) + " = WGodotNative::convert<" + type(p_target->type_constraint, p_target) + ">(" + p_value + ")";
		default:
			return property_access(current_class->node->self_type, p_target->name, p_target, "this", p_value);
	}
}

String WGodotCppEmitter::assignment(const Parser::AssignmentNode *p_assignment) {
	class_call_headers.insert("modules/wgodot/native/wgodot_native_values.h");
	const auto *target = p_assignment->assignee;
	const auto *assigned = p_assignment->assigned_value;
	const bool compound = p_assignment->operation != Parser::AssignmentNode::OP_NONE;
	const auto target_type = expression_type(target);
	if (!compound && !validate_array_conversion(assigned, target_type, target)) {
		return String();
	}
	const auto assigned_type = assigned->type == Parser::Node::ARRAY && is_warray(target_type) ? target_type : expression_type(assigned);
	const String assigned_value = assigned->type == Parser::Node::ARRAY && is_warray(target_type) ? array_literal(static_cast<const Parser::ArrayNode *>(assigned), target_type) : expression(assigned);
	String body = "([&]() { ";
	if (target->type == Parser::Node::IDENTIFIER) {
		body += "auto &&value = " + assigned_value + "; ";
		if (compound) {
			body += "auto &&previous = " + expression(target) + "; auto result = " + operation(p_assignment->variant_op, target_type, target_type, assigned_type, "previous", "value", p_assignment) + "; ";
		}
		return body + store_identifier(static_cast<const Parser::IdentifierNode *>(target), compound ? "result" : "value") + "; }())";
	}
	if (target->type != Parser::Node::SUBSCRIPT) {
		unsupported(target, "assignment target");
		return String();
	}
	Vector<const Parser::SubscriptNode *> chain;
	const Parser::ExpressionNode *root = target;
	while (root->type == Parser::Node::SUBSCRIPT) {
		const auto *node = static_cast<const Parser::SubscriptNode *>(root);
		chain.insert(0, node);
		root = node->base;
	}
	// Borrow the root slot, but own intermediate get results as the language does.
	// Read the RHS before the final index, and only then read a compound target.
	if (!root->type_constraint.is_meta_type) {
		body += "auto &&base_0 = " + expression(root) + "; ";
	}
	auto key = [&](int p_index) { return "key_" + itos(p_index); };
	auto base = [&](int p_index) { return "base_" + itos(p_index); };
	auto read = [&](int p_index) {
		const auto *node = chain[p_index];
		if (p_index + 1 < chain.size()) {
			if (const String *saved = expression_overrides.getptr(node)) {
				return *saved;
			}
		}
		return node->is_attribute ? property_access(node->base->type_constraint, node->attribute->name, node, base(p_index)) : "WGodotNative::get_index<" + type(node->type_constraint, node) + ">(" + base(p_index) + ", " + key(p_index) + ")";
	};
	auto write = [&](int p_index, const String &p_value) {
		const auto *node = chain[p_index];
		return node->is_attribute ? property_access(node->base->type_constraint, node->attribute->name, node, base(p_index), p_value) : "WGodotNative::set_index(" + base(p_index) + ", " + key(p_index) + ", " + p_value + ")";
	};
	for (int i = 0; i < chain.size(); i++) {
		if (i == chain.size() - 1) {
			body += "auto &&value = " + assigned_value + "; ";
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
		body += "auto previous = " + read(leaf) + "; auto result = " + operation(p_assignment->variant_op, target_type, target_type, assigned_type, "previous", "value", p_assignment) + "; ";
	}
	body += write(leaf, compound ? "result" : "value") + "; ";
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
			body += write_back(datatype, "base_0", store_identifier(identifier, "base_0"));
		}
	}
	return body + "}())";
}
