// wgodot-changes::file
#include "gdscript_compiler.h"

#include "gdscript.h"
#include "wgodot_value_paths.h"

using Parser = GDScriptParser;
using Address = GDScriptCodeGenerator::Address;

bool GDScriptCompiler::_wgodot_assign_value_path(CodeGen &p_codegen, Error &r_error, const Parser::AssignmentNode *p_assignment) {
	if (p_assignment->assignee->type != Parser::Node::SUBSCRIPT || !WGodotValuePaths::stable(p_assignment->assigned_value)) {
		return false;
	}
	if (!WGodotValuePaths::plain_data(p_assignment->assignee->type_constraint)) {
		return false;
	}
	Vector<const Parser::SubscriptNode *> chain;
	const Parser::ExpressionNode *root = p_assignment->assignee;
	while (root->type == Parser::Node::SUBSCRIPT) {
		const auto *element = static_cast<const Parser::SubscriptNode *>(root);
		if (!element->is_attribute && !WGodotValuePaths::stable(element->index)) {
			return false;
		}
		chain.push_back(element);
		root = element->base;
	}
	Address base;
	StringName member_name;
	if (root->type == Parser::Node::SELF && chain[chain.size() - 1]->is_attribute) {
		member_name = chain[chain.size() - 1]->attribute->name;
		chain.resize(chain.size() - 1);
	} else if (root->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(root);
		if (!_is_local_or_parameter(p_codegen, identifier->name)) {
			member_name = identifier->name;
		}
	} else {
		return false;
	}
	if (chain.is_empty()) {
		return false;
	}
	if (member_name != StringName()) {
		const auto *member = p_codegen.script->member_indices.getptr(member_name);
		if (!member || member->getter != StringName() || member->setter != StringName()) {
			return false;
		}
		base = Address(Address::MEMBER, member->index, member->data_type);
	}
	// All intermediate slots must live in COW values. The final setter handles
	// its own type conversion, including scalar widths and packed elements.
	for (int index = chain.size() - 1; index > 0; --index) {
		if (!WGodotValuePaths::owner(chain[index]->base->type_constraint)) {
			return false;
		}
	}
	const auto &destination = chain[0]->base->type_constraint;
	if (destination.is_variant() || (destination.kind != Parser::DataType::BUILTIN && !WGodotValuePaths::owner(destination))) {
		return false;
	}
	if (p_assignment->operation != Parser::AssignmentNode::OP_NONE &&
			(p_assignment->assignee->type_constraint.builtin_type == Variant::OBJECT || p_assignment->assigned_value->type_constraint.builtin_type == Variant::OBJECT)) {
		return false;
	}
	if (member_name == StringName()) {
		base = _parse_expression(p_codegen, r_error, root);
		if (r_error != OK) {
			return true;
		}
	}
	Address assigned = _parse_expression(p_codegen, r_error, p_assignment->assigned_value);
	if (r_error != OK) {
		return true;
	}
	int temporaries = assigned.mode == Address::TEMPORARY ? 1 : 0;
	if (assigned.type.builtin_type == Variant::WRECORD || assigned.type.wgodot_value) {
		const Address snapshot = p_codegen.add_temporary(assigned.type);
		p_codegen.generator->write_assign(snapshot, assigned);
		assigned = snapshot;
		++temporaries;
	}
	Vector<Address> keys;
	for (int index = chain.size() - 1; index >= 0; --index) {
		const auto *element = chain[index];
		Address key = element->is_attribute ? p_codegen.add_constant(element->attribute->name) : _parse_expression(p_codegen, r_error, element->index);
		if (r_error != OK) {
			return true;
		}
		temporaries += key.mode == Address::TEMPORARY ? 1 : 0;
		if (key.type.builtin_type == Variant::WRECORD || key.type.wgodot_value) {
			const Address snapshot = p_codegen.add_temporary(key.type);
			p_codegen.generator->write_assign(snapshot, key);
			key = snapshot;
			++temporaries;
		}
		keys.push_back(key);
	}
	p_codegen.generator->write_set_value_path(base, keys, assigned, p_assignment->operation == Parser::AssignmentNode::OP_NONE ? Variant::OP_MAX : p_assignment->variant_op);
	for (int index = 0; index < temporaries; ++index) {
		p_codegen.generator->pop_temporary();
	}
	return true;
}

bool GDScriptCompiler::_wgodot_mutating_value_call(CodeGen &p_codegen, Error &r_error, const Parser::CallNode *p_call, const Address &p_result, const Vector<Address> &p_arguments) {
	if (p_call->is_super || p_call->callee->type != Parser::Node::SUBSCRIPT) {
		return false;
	}
	const auto *callee = static_cast<const Parser::SubscriptNode *>(p_call->callee);
	const Parser::ExpressionNode *receiver = callee->base;
	const auto &type = receiver->type_constraint;
	if (!callee->is_attribute || !type.wgodot_value ||
			Variant::is_builtin_method_const(type.builtin_type, p_call->function_name) ||
			Variant::is_builtin_method_static(type.builtin_type, p_call->function_name)) {
		return false;
	}

	Vector<const Parser::SubscriptNode *> chain;
	const Parser::ExpressionNode *root = receiver;
	while (root->type == Parser::Node::SUBSCRIPT) {
		const auto *element = static_cast<const Parser::SubscriptNode *>(root);
		chain.push_back(element);
		root = element->base;
	}
	Address base = _parse_expression(p_codegen, r_error, root);
	if (r_error != OK) {
		return true;
	}
	int temporaries = base.mode == Address::TEMPORARY ? 1 : 0;
	StringName root_property;
	StringName root_setter;
	Address static_class;
	int static_index = -1;
	if (root->type == Parser::Node::IDENTIFIER) {
		const StringName name = static_cast<const Parser::IdentifierNode *>(root)->name;
		if (!_is_local_or_parameter(p_codegen, name)) {
			if (_is_class_member_property(p_codegen, name)) {
				root_property = name;
			} else if (const GDScript::MemberInfo *member = p_codegen.script->member_indices.getptr(name)) {
				if (member->setter != p_codegen.function_name) {
					root_setter = member->setter;
				}
			} else {
				for (GDScript *script = p_codegen.script; script; script = script->base.ptr()) {
					if (const GDScript::MemberInfo *member = script->static_variables_indices.getptr(name)) {
						static_class = p_codegen.add_constant(script);
						static_index = member->index;
						if (member->setter != p_codegen.function_name) {
							root_setter = member->setter;
						}
						break;
					}
				}
			}
		}
	}
	if (root_setter != StringName() && base.mode != Address::TEMPORARY) {
		const Address captured = p_codegen.add_temporary(base.type);
		p_codegen.generator->write_assign(captured, base);
		base = captured;
		++temporaries;
	}
	if (root->type == Parser::Node::SELF && !chain.is_empty() && chain[chain.size() - 1]->is_attribute) {
		const StringName name = chain[chain.size() - 1]->attribute->name;
		if (const GDScript::MemberInfo *member = p_codegen.script->member_indices.getptr(name)) {
			if (member->getter == StringName() && member->setter == StringName()) {
				base = Address(Address::MEMBER, member->index, member->data_type);
				chain.resize(chain.size() - 1);
			}
		}
	}
	bool borrowable = !chain.is_empty() && (base.mode == Address::LOCAL_VARIABLE || base.mode == Address::FUNCTION_PARAMETER || base.mode == Address::MEMBER) &&
			root_setter == StringName() && root_property == StringName() && WGodotValuePaths::plain_data(type);
	for (int index = 0; borrowable && index < Variant::get_builtin_method_argument_count(type.builtin_type, p_call->function_name); ++index) {
		// User callbacks must observe the unchanged owner until writeback.
		borrowable = Variant::get_builtin_method_argument_type(type.builtin_type, p_call->function_name, index) != Variant::CALLABLE;
	}
	GDScriptDataType owner_type = base.type;
	for (int index = chain.size() - 1; borrowable && index >= 0; --index) {
		const auto *element = chain[index];
		const bool record = owner_type.builtin_type == Variant::WRECORD;
		const bool container = owner_type.wgodot_value && (owner_type.builtin_type == Variant::ARRAY || owner_type.builtin_type == Variant::DICTIONARY);
		borrowable = record || container;
		if (!element->is_attribute) {
			const auto *key = element->index;
			const bool local = key->type == Parser::Node::IDENTIFIER && _is_local_or_parameter(p_codegen, static_cast<const Parser::IdentifierNode *>(key)->name);
			borrowable &= key->is_constant || local;
		}
		owner_type = _gdtype_from_datatype(element->type_constraint, p_codegen.script);
	}
	if (borrowable) {
		// An argument may be the owner itself. Retain its value before
		// detaching the receiver path, so inserting it cannot form a cycle.
		Vector<Address> arguments = p_arguments;
		for (int index = 0; index < arguments.size(); ++index) {
			const Address argument = arguments[index];
			if (argument.type.builtin_type == Variant::WRECORD || argument.type.wgodot_value) {
				const Address snapshot = p_codegen.add_temporary(argument.type);
				p_codegen.generator->write_assign(snapshot, argument);
				arguments.write[index] = snapshot;
				++temporaries;
			}
		}
		Vector<Address> keys;
		Vector<Address> owners;
		for (int index = chain.size() - 1; index >= 0; --index) {
			const auto *element = chain[index];
			Address key = element->is_attribute ? p_codegen.add_constant(element->attribute->name) : _parse_expression(p_codegen, r_error, element->index);
			if (r_error != OK) {
				return true;
			}
			if (key.type.builtin_type == Variant::WRECORD || key.type.wgodot_value) {
				const Address snapshot = p_codegen.add_temporary(key.type);
				p_codegen.generator->write_assign(snapshot, key);
				key = snapshot;
				++temporaries;
			}
			keys.push_back(key);
			owners.push_back(p_codegen.add_temporary(GDScriptDataType()));
			++temporaries;
		}
		p_codegen.generator->write_call_value_builtin(p_result, base, keys, owners, p_call->function_name, arguments);
		for (int index = 0; index < temporaries; ++index) {
			p_codegen.generator->pop_temporary();
		}
		return true;
	}

	struct Link {
		Address owner;
		Address key;
		StringName name;
		bool named;
	};
	Vector<Link> links;
	for (int index = chain.size() - 1; index >= 0; --index) {
		const auto *element = chain[index];
		Link link{ base, Address(), StringName(), element->is_attribute };
		if (link.named) {
			link.name = element->attribute->name;
		} else {
			link.key = _parse_expression(p_codegen, r_error, element->index);
			if (r_error != OK) {
				return true;
			}
			temporaries += link.key.mode == Address::TEMPORARY ? 1 : 0;
		}
		Address value = p_codegen.add_temporary(_gdtype_from_datatype(element->type_constraint, p_codegen.script));
		++temporaries;
		if (link.named) {
			p_codegen.generator->write_get_named(value, link.name, base);
		} else {
			p_codegen.generator->write_get(value, link.key, base);
		}
		links.push_back(link);
		base = value;
	}
	p_codegen.generator->write_call_builtin_type(p_result, base, type.builtin_type, p_call->function_name, p_arguments);
	for (int index = links.size() - 1; index >= 0; --index) {
		if (Variant::is_type_shared(base.type.builtin_type) && !base.type.wgodot_value) {
			break;
		}
		const Link &link = links[index];
		if (link.named) {
			p_codegen.generator->write_set_named(link.owner, link.name, base);
		} else {
			p_codegen.generator->write_set(link.owner, link.key, base);
		}
		base = link.owner;
	}
	if (!Variant::is_type_shared(base.type.builtin_type) || base.type.wgodot_value) {
		if (root_property != StringName()) {
			_wgodot_set_member(p_codegen, base, root_property);
		} else if (root_setter != StringName()) {
			Vector<Address> arguments;
			arguments.push_back(base);
			p_codegen.generator->write_call(Address(), static_index >= 0 ? static_class : Address(Address::SELF), root_setter, arguments);
		} else if (static_index >= 0) {
			p_codegen.generator->write_set_static_variable(base, static_class, static_index);
		}
	}
	for (int index = 0; index < temporaries; ++index) {
		p_codegen.generator->pop_temporary();
	}
	return true;
}
