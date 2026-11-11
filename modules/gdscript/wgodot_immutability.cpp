// wgodot-changes::file
#include "gdscript_analyzer.h"

#include "gdscript.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "wgodot_builtin_effects.gen.h"

namespace {
using Parser = GDScriptParser;
using Type = Parser::DataType;

bool is_data_container(const Type &p_type) {
	return p_type.kind == Type::BUILTIN && (p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY ||
			(p_type.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY));
}

bool is_null(const Type &p_type) {
	return p_type.kind == Type::BUILTIN && p_type.builtin_type == Variant::NIL;
}

bool is_self(const Parser::ExpressionNode *p_expression) {
	return p_expression && p_expression->type == Parser::Node::SELF;
}

void merge_qualifications(Type &r_target, const Type &p_source) {
	r_target.wgodot_immutable |= p_source.wgodot_immutable;
	for (int i = 0; i < p_source.get_container_element_type_count(); i++) {
		Type child = r_target.get_container_element_type_or_variant(i);
		merge_qualifications(child, p_source.get_container_element_type(i));
		r_target.set_container_element_type(i, child);
	}
}

const Parser::ExpressionNode *behavioral_origin(const Parser::ExpressionNode *p_value) {
	if (p_value->type == Parser::Node::CAST) {
		return behavioral_origin(static_cast<const Parser::CastNode *>(p_value)->operand);
	}
	if (p_value->type == Parser::Node::AWAIT) {
		return behavioral_origin(static_cast<const Parser::AwaitNode *>(p_value)->to_await);
	}
	if (p_value->type == Parser::Node::TERNARY_OPERATOR) {
		const auto *branches = static_cast<const Parser::TernaryOpNode *>(p_value);
		if (const auto *origin = behavioral_origin(branches->true_expr)) {
			return origin;
		}
		return behavioral_origin(branches->false_expr);
	}
	return p_value->type_constraint.kind == Type::CLASS && !p_value->type_constraint.class_type->wgodot_data_class ? p_value : nullptr;
}
} // namespace

void GDScriptAnalyzer::wgodot_validate_data_class(Parser::ClassNode *p_class) {
	if (p_class->base_type.class_type && p_class->base_type.class_type->wgodot_data_class) {
		p_class->wgodot_data_class = true;
	}
	if (!p_class->wgodot_data_class) {
		return;
	}
	if (p_class->wgodot_static_class) {
		push_error("@data_class describes instances and cannot be combined with @static_class.", p_class);
	}
	if (p_class->base_type.kind == Type::CLASS) {
		if (!p_class->base_type.class_type->wgodot_data_class) {
			push_error("A data class cannot inherit a behavioral script class.", p_class);
		}
	} else if (p_class->base_type.kind != Type::NATIVE ||
			(p_class->base_type.native_type != SNAME("Object") && p_class->base_type.native_type != SNAME("RefCounted") && p_class->base_type.native_type != SNAME("Resource"))) {
		push_error("A data class must inherit another data class, Object, RefCounted or Resource.", p_class);
	}
}

bool GDScriptAnalyzer::wgodot_validate_immutable_type(const Type &p_type, const Parser::Node *p_source) {
	if (!p_type.wgodot_immutable) {
		return true;
	}
	if (p_type.is_meta_type) {
		push_error("@immutable qualifies instances, not class objects.", p_source);
		return false;
	}
	if (is_data_container(p_type)) {
		return true;
	}
	if (p_type.kind == Type::NATIVE && ClassDB::is_parent_class(p_type.native_type, SNAME("Resource"))) {
		return true;
	}
	if (p_type.kind == Type::CLASS && p_type.class_type) {
		resolve_class_inheritance(p_type.class_type, p_source);
		if (p_type.class_type->wgodot_data_class) {
			return true;
		}
	}
	push_error(vformat("@immutable requires a data class or data container; '%s' is not eligible.", p_type.to_string()), p_source);
	return false;
}

bool GDScriptAnalyzer::wgodot_qualify_native_resources(Type &r_type) {
	bool qualified = false;
	if (r_type.kind == Type::NATIVE && !r_type.is_meta_type && ClassDB::is_parent_class(r_type.native_type, SNAME("Resource"))) {
		r_type.wgodot_immutable = true;
		qualified = true;
	}
	for (int i = 0; i < r_type.get_container_element_type_count(); i++) {
		Type element = r_type.get_container_element_type(i);
		qualified |= wgodot_qualify_native_resources(element);
		r_type.set_container_element_type(i, element);
	}
	return qualified;
}

bool GDScriptAnalyzer::wgodot_is_native_packed_property(const Parser::ExpressionNode *p_expression) {
	if (p_expression->type_constraint.builtin_type < Variant::PACKED_BYTE_ARRAY ||
			p_expression->type_constraint.builtin_type > Variant::PACKED_VECTOR4_ARRAY) {
		return false;
	}
	if (p_expression->type == Parser::Node::SUBSCRIPT) {
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
		return subscript->is_attribute && wgodot_is_native_packed_property(subscript->attribute);
	}
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		return identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE;
	}
	return false;
}

void GDScriptAnalyzer::wgodot_native_resource_signature(const StringName &p_class, const StringName &p_method, Parser::Node *p_source, Type &r_result, List<Type> &r_parameters) {
	wgodot_qualify_native_resources(r_result);
	const MethodBind *method = ClassDB::get_method(p_class, p_method);
	if (method && method->get_instance_class() == SNAME("Resource") &&
			(p_method == SNAME("duplicate") || p_method == SNAME("duplicate_deep")) && p_source->type == Parser::Node::CALL) {
		auto *call = static_cast<Parser::CallNode *>(p_source);
		if (call->get_callee_type() == Parser::Node::SUBSCRIPT) {
			r_result = static_cast<const Parser::SubscriptNode *>(call->callee)->base->type_constraint;
		}
		r_result.wgodot_immutable = false;
		call->wgodot_fresh_value = true;
	}
	if (r_parameters.is_empty()) {
		return;
	}
	Type value_type = r_parameters.back()->get();
	if (!wgodot_qualify_native_resources(value_type)) {
		return;
	}
	// Setter methods and property syntax have the same contract. Other native
	// functions need their own audited argument contract; a const Ref<T> alone
	// does not promise that the pointed-to Resource is left unchanged.
	List<PropertyInfo> properties;
	ClassDB::get_property_list(p_class, &properties);
	for (const auto &property : properties) {
		if (ClassDB::get_property_setter(p_class, property.name) == p_method) {
			r_parameters.back()->get() = value_type;
			break;
		}
	}
}

void GDScriptAnalyzer::wgodot_validate_native_callable(const Type &p_base, const MethodInfo &p_method, const Parser::Node *p_source) {
	if (p_base.wgodot_immutable && !(p_method.flags & (METHOD_FLAG_CONST | METHOD_FLAG_STATIC))) {
		push_error("Cannot bind a mutating native method through an @immutable reference.", p_source);
	}
	Type result = type_from_property(p_method.return_val, false, p_source);
	bool qualified_signature = wgodot_qualify_native_resources(result);
	if (p_base.kind == Type::BUILTIN && wgodot_has_immutable_type(p_base)) {
		// MethodInfo cannot represent qualified container elements. Direct calls
		// retain the receiver's AST type; an ordinary native Callable cannot.
		qualified_signature |= result.builtin_type == Variant::ARRAY || result.builtin_type == Variant::DICTIONARY || result.is_variant();
		for (const auto &parameter : p_method.arguments) {
			qualified_signature |= parameter.type == Variant::ARRAY || parameter.type == Variant::DICTIONARY || parameter.type == Variant::NIL;
		}
	}
	if (qualified_signature) {
		push_error("This native Callable would erase immutable qualifications; use a typed wrapper function with a direct call.", p_source);
	}
}

bool GDScriptAnalyzer::wgodot_has_immutable_type(const Type &p_type) {
	if (p_type.wgodot_immutable) {
		return true;
	}
	for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
		if (wgodot_has_immutable_type(p_type.get_container_element_type(i))) {
			return true;
		}
	}
	return false;
}

bool GDScriptAnalyzer::wgodot_immutable_types_match(const Type &p_target, const Type &p_source, bool p_fresh) {
	if (is_null(p_source)) {
		return true; // Ordinary null/type compatibility is checked separately.
	}
	if (p_target.wgodot_immutable != p_source.wgodot_immutable && !(p_target.wgodot_immutable && p_fresh)) {
		return false;
	}
	const auto *target_function = p_target.wgodot_callable_signature;
	const auto *source_function = p_source.wgodot_callable_signature;
	if (target_function != source_function && (target_function || source_function)) {
		const auto qualified = [](const Parser::FunctionNode *p_function) {
			if (!p_function) {
				return false;
			}
			if (p_function->wgodot_immutable_return || p_function->wgodot_immutable_clone || wgodot_has_immutable_type(p_function->return_type_constraint)) {
				return true;
			}
			for (const auto *parameter : p_function->parameters) {
				if (wgodot_has_immutable_type(parameter->type_constraint)) {
					return true;
				}
			}
			return false;
		};
		if (qualified(target_function) || qualified(source_function)) {
			if (!target_function || !source_function || target_function->parameters.size() != source_function->parameters.size() ||
					target_function->wgodot_immutable_clone != source_function->wgodot_immutable_clone ||
					target_function->wgodot_immutable_return != source_function->wgodot_immutable_return) {
				return false;
			}
			for (uint32_t i = 0; i < target_function->parameters.size(); i++) {
				if (!wgodot_immutable_types_match(target_function->parameters[i]->type_constraint, source_function->parameters[i]->type_constraint)) {
					return false;
				}
			}
			if (!wgodot_immutable_types_match(target_function->return_type_constraint, source_function->return_type_constraint)) {
				return false;
			}
		}
	}
	const int count = MAX(p_target.get_container_element_type_count(), p_source.get_container_element_type_count());
	for (int i = 0; i < count; i++) {
		if (!wgodot_immutable_types_match(p_target.get_container_element_type_or_variant(i), p_source.get_container_element_type_or_variant(i))) {
			return false;
		}
	}
	return true;
}

bool GDScriptAnalyzer::wgodot_validate_immutable_value(const Type &p_target, const Parser::ExpressionNode *p_value) {
	if (p_value && p_target.wgodot_immutable && p_value->wgodot_fresh_value) {
		if (const auto *origin = behavioral_origin(p_value)) {
			push_error("Fresh script objects must be data classes before they can acquire an immutable reference, including through a native base type.", origin);
			return false;
		}
	}
	if (!p_value || wgodot_immutable_types_match(p_target, p_value->type_constraint, p_value->wgodot_fresh_value)) {
		return true;
	}
	push_error("Cannot discard or manufacture @immutable qualification. Use a matching declaration or a verified independent copy.", p_value);
	return false;
}

void GDScriptAnalyzer::wgodot_validate_immutable_argument(const Type &p_target, const Parser::ExpressionNode *p_value, const Parser::CallNode *p_call) {
	if (p_call->wgodot_native_call) {
		// Typed native packed-array arguments cross the ABI as Vector<T> values
		// or const references. Retained copies have COW storage, not the shared
		// GDScript packed-array wrapper through which script mutation occurs.
		if (p_target.kind == Type::BUILTIN && p_target.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_target.builtin_type <= Variant::PACKED_VECTOR4_ARRAY) {
			return;
		}
		if (p_call->get_callee_type() == Parser::Node::SUBSCRIPT && !p_call->arguments.is_empty() && p_call->arguments[0] == p_value) {
			const Type &receiver = static_cast<const Parser::SubscriptNode *>(p_call->callee)->base->type_constraint;
			const StringName &method = p_call->function_name;
			const bool reads_container =
					(receiver.builtin_type == Variant::ARRAY && (method == SNAME("assign") || method == SNAME("append_array"))) ||
					(receiver.builtin_type == Variant::DICTIONARY && (method == SNAME("assign") || method == SNAME("merge") || method == SNAME("merged") || method == SNAME("has_all")));
			if (reads_container) {
				// These operations copy entries without retaining or mutating the
				// source container. Element qualifications still have to agree.
				Type input = p_target;
				input.wgodot_immutable = p_value->type_constraint.wgodot_immutable;
				wgodot_validate_immutable_value(input, p_value);
				return;
			}
		}
		if (p_call->get_callee_type() == Parser::Node::IDENTIFIER) {
			const StringName &name = p_call->function_name;
			if (name == SNAME("len") || name == SNAME("typeof") || name == SNAME("is_instance_valid") || name == SNAME("is_same") ||
					name == SNAME("print") || name == SNAME("prints") || name == SNAME("printt") || name == SNAME("print_rich") ||
					name == SNAME("printerr") || name == SNAME("printraw") || name == SNAME("push_error") || name == SNAME("push_warning") || name == SNAME("str")) {
				return;
			}
		}
	}
	wgodot_validate_immutable_value(p_target, p_value);
}

void GDScriptAnalyzer::wgodot_resolve_immutable_variable(Parser::AssignableNode *p_variable, Type &r_type) {
	// Inference must not silently turn an ordinary binding into an immutable one.
	r_type.wgodot_immutable = p_variable->wgodot_immutable || (p_variable->datatype_specifier && p_variable->datatype_specifier->wgodot_immutable);
	if (r_type.builtin_type == Variant::CALLABLE && p_variable->initializer) {
		r_type.wgodot_callable_signature = p_variable->initializer->type_constraint.wgodot_callable_signature;
	}
	wgodot_validate_immutable_type(r_type, p_variable);
	wgodot_validate_immutable_value(r_type, p_variable->initializer);
}

bool GDScriptAnalyzer::wgodot_reduce_mutable_alias(Parser::CallNode *p_call) {
	if (p_call->get_callee_type() != Parser::Node::IDENTIFIER || p_call->function_name != SNAME("dangerous_return_as_mutable")) {
		return false;
	}
	if (p_call->arguments.size() != 2) {
		push_error("dangerous_return_as_mutable requires a type and an object or container.", p_call);
		return true;
	}
	auto *type_expression = p_call->arguments[0];
	if (type_expression->type == Parser::Node::IDENTIFIER) {
		reduce_identifier(static_cast<Parser::IdentifierNode *>(type_expression), true);
	} else if (type_expression->type == Parser::Node::SUBSCRIPT) {
		reduce_subscript(static_cast<Parser::SubscriptNode *>(type_expression), true);
	} else {
		push_error("The first argument must name a statically known type.", type_expression);
		return true;
	}
	if (!type_expression->type_constraint.is_meta_type) {
		push_error("The first argument must name a type, not an instance.", type_expression);
		return true;
	}
	reduce_expression(p_call->arguments[1]);
	Type target = type_from_metatype(type_expression->type_constraint);
	const Type &source = p_call->arguments[1]->type_constraint;
	if (!is_type_compatible(target, source, false, p_call)) {
		push_error("dangerous_return_as_mutable cannot change the object's type.", p_call);
	}
	if (target.kind == Type::BUILTIN && target.builtin_type == source.builtin_type) {
		target.container_element_types = source.container_element_types;
	}
	target.wgodot_immutable = false;
	p_call->type_constraint = target;
	p_call->wgodot_mutable_alias = true;
	return true;
}

void GDScriptAnalyzer::wgodot_resolve_immutable_function(Parser::FunctionNode *p_function) {
	if (p_function->wgodot_immutable_return || p_function->wgodot_immutable_clone) {
		Type result = p_function->return_type_constraint;
		result.wgodot_immutable = true;
		wgodot_validate_immutable_type(result, p_function);
		if (p_function->wgodot_immutable_return) {
			p_function->return_type_constraint.wgodot_immutable = true;
		}
		if (p_function->wgodot_immutable_clone && (result.kind != Type::CLASS || result.class_type != parser->current_class || !parser->current_class->wgodot_data_class)) {
			push_error("@immutable_clone must return its own data-class type.", p_function);
		}
	}
}

void GDScriptAnalyzer::wgodot_validate_immutable_assignment(Parser::AssignmentNode *p_assignment) {
	const bool readonly_self = parser->current_function && parser->current_function->wgodot_readonly;
	const Parser::ExpressionNode *target = p_assignment->assignee;
	while (target) {
		if (target->type == Parser::Node::IDENTIFIER) {
			const auto *identifier = static_cast<const Parser::IdentifierNode *>(target);
			if (readonly_self && (identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE || identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE)) {
				push_error("A @readonly method cannot assign a direct member of self.", p_assignment);
			}
			break;
		}
		if (target->type != Parser::Node::SUBSCRIPT) {
			break;
		}
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(target);
		const Type &base = subscript->base->type_constraint;
		if (base.wgodot_immutable || (readonly_self && is_self(subscript->base))) {
			push_error("Cannot write through an @immutable reference or a @readonly receiver.", p_assignment);
			break;
		}
		// Value-type component writes modify their containing value; child objects
		// and containers are independent references under the shallow contract.
		if (base.kind != Type::BUILTIN || is_data_container(base)) {
			break;
		}
		target = subscript->base;
	}
}

void GDScriptAnalyzer::wgodot_validate_immutable_call(Parser::CallNode *p_call, const Type &p_base, BitField<MethodFlags> p_flags, bool p_self, bool p_constructor) {
	const bool readonly_self = p_self && parser->current_function && parser->current_function->wgodot_readonly;
	p_call->wgodot_readonly_call = p_flags.has_flag(METHOD_FLAG_CONST) || p_flags.has_flag(METHOD_FLAG_STATIC) || p_constructor;
	const bool constructor_self = p_self && parser->current_class->wgodot_data_class && parser->current_function &&
			parser->current_function->identifier && parser->current_function->identifier->name == GDScriptLanguage::get_singleton()->strings._init;
	if (constructor_self && !p_constructor && !p_flags.has_flag(METHOD_FLAG_STATIC) && !p_flags.has_flag(METHOD_FLAG_CONST) &&
			!(p_call->is_super && p_call->function_name == GDScriptLanguage::get_singleton()->strings._init)) {
		push_error("A data-class constructor may only call readonly self helpers or its parent constructor.", p_call);
	}
	if (!p_constructor && !p_flags.has_flag(METHOD_FLAG_STATIC) && (p_base.wgodot_immutable || readonly_self) && !p_flags.has_flag(METHOD_FLAG_CONST)) {
		push_error("An @immutable reference or @readonly receiver can only call readonly methods.", p_call);
	}
	if (p_constructor || (p_call->wgodot_resolved_function && p_call->wgodot_resolved_function->wgodot_immutable_clone)) {
		p_call->wgodot_fresh_value = true;
	}
	if (p_base.kind == Type::BUILTIN) {
		for (const auto &contract : wgodot_fresh_builtin_results) {
			if (p_base.builtin_type == contract.type && p_call->function_name == contract.method) {
				p_call->wgodot_fresh_value = true;
				break;
			}
		}
	}
}

void GDScriptAnalyzer::wgodot_reduce_immutable_expression(Parser::ExpressionNode *p_expression) {
	switch (p_expression->type) {
		case Parser::Node::IDENTIFIER:
		case Parser::Node::SUBSCRIPT:
			// Native packed-array getters return a separate Vector wrapper with
			// COW storage. Script fields instead preserve their wrapper identity.
			p_expression->wgodot_fresh_value = wgodot_is_native_packed_property(p_expression);
			break;
		case Parser::Node::ARRAY: {
			Type element = Type::get_variant_type();
			for (const auto *value : static_cast<const Parser::ArrayNode *>(p_expression)->elements) {
				merge_qualifications(element, value->type_constraint);
			}
			if (wgodot_has_immutable_type(element)) {
				p_expression->type_constraint.set_container_element_type(0, element);
			}
			p_expression->wgodot_fresh_value = true;
		} break;
		case Parser::Node::DICTIONARY: {
			Type key = Type::get_variant_type();
			Type value = Type::get_variant_type();
			for (const auto &entry : static_cast<const Parser::DictionaryNode *>(p_expression)->elements) {
				merge_qualifications(key, entry.key->type_constraint);
				merge_qualifications(value, entry.value->type_constraint);
			}
			if (wgodot_has_immutable_type(key) || wgodot_has_immutable_type(value)) {
				p_expression->type_constraint.set_container_element_type(0, key);
				p_expression->type_constraint.set_container_element_type(1, value);
			}
			p_expression->wgodot_fresh_value = true;
		} break;
		case Parser::Node::AWAIT:
			p_expression->wgodot_fresh_value = static_cast<const Parser::AwaitNode *>(p_expression)->to_await->wgodot_fresh_value;
			break;
		case Parser::Node::BINARY_OPERATOR: {
			const auto *operation = static_cast<const Parser::BinaryOpNode *>(p_expression);
			if (operation->variant_op == Variant::OP_ADD && p_expression->type_constraint.builtin_type == Variant::ARRAY) {
				Type left = operation->left_operand->type_constraint;
				Type right = operation->right_operand->type_constraint;
				left.wgodot_immutable = right.wgodot_immutable = false;
				if (!wgodot_immutable_types_match(left, right)) {
					push_error("Array concatenation must preserve element qualifications.", p_expression);
				}
				p_expression->type_constraint.wgodot_immutable = false;
				p_expression->wgodot_fresh_value = true;
			}
		} break;
		case Parser::Node::CALL: {
			auto *call = static_cast<Parser::CallNode *>(p_expression);
			if (call->wgodot_native_call && call->type_constraint.kind == Type::BUILTIN &&
					call->type_constraint.builtin_type >= Variant::PACKED_BYTE_ARRAY && call->type_constraint.builtin_type <= Variant::PACKED_VECTOR4_ARRAY) {
				call->wgodot_fresh_value = true;
			}
			if (call->get_callee_type() == Parser::Node::IDENTIFIER) {
				const Variant::Type constructed = Parser::get_builtin_type(call->function_name);
				if (constructed < Variant::VARIANT_MAX && is_data_container(call->type_constraint)) {
					// A same-type constructor aliases its input. Converting a literal
					// or another container representation builds independent storage.
					const bool aliases = call->arguments.size() == 1 && call->arguments[0]->type_constraint.builtin_type == constructed;
					call->wgodot_fresh_value = !aliases || call->arguments[0]->wgodot_fresh_value;
					if (aliases) {
						call->type_constraint = call->arguments[0]->type_constraint;
					}
				}
			}
		} break;
		case Parser::Node::PRELOAD:
			if (!p_expression->type_constraint.is_meta_type && !wgodot_is_ignored_script()) {
				p_expression->type_constraint.wgodot_immutable = true;
				wgodot_validate_immutable_type(p_expression->type_constraint, p_expression);
			}
			break;
		case Parser::Node::TERNARY_OPERATOR: {
			const auto *ternary = static_cast<Parser::TernaryOpNode *>(p_expression);
			Type common = p_expression->type_constraint;
			common.wgodot_immutable = ternary->true_expr->type_constraint.wgodot_immutable || ternary->false_expr->type_constraint.wgodot_immutable;
			if (!wgodot_immutable_types_match(common, ternary->true_expr->type_constraint, ternary->true_expr->wgodot_fresh_value) ||
					!wgodot_immutable_types_match(common, ternary->false_expr->type_constraint, ternary->false_expr->wgodot_fresh_value)) {
				push_error("Both branches of a conditional expression must retain the same immutable qualifications.", p_expression);
			}
			p_expression->type_constraint = common;
			p_expression->wgodot_fresh_value = ternary->true_expr->wgodot_fresh_value && ternary->false_expr->wgodot_fresh_value;
		} break;
		default:
			break;
	}
}
