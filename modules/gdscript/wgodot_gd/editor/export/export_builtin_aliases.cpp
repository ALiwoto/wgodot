// wgodot-changes::file
/**************************************************************************/
/*  export_builtin_aliases.cpp                                            */
/**************************************************************************/

#include "export_ast_visitor.h"
#include "export_transform_internal.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/templates/list.h"
#include "core/variant/variant.h"

#include "modules/gdscript/gdscript_utility_functions.h"

namespace WGodotGDScriptExportTransform {

bool is_supported_builtin_class_alias_target(const StringName &p_name) {
	if (p_name.is_empty()) {
		return false;
	}

	if (ClassDB::class_exists(p_name) && ClassDB::is_class_exposed(p_name)) {
		return true;
	}

	return GDScriptParser::get_builtin_type(p_name) < Variant::VARIANT_MAX;
}

void get_or_create_builtin_class_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_class_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_class_alias(p_name);
}

bool is_supported_builtin_function_alias_target(const StringName &p_name) {
	// The analyzer and compiler recognize range() by name for typed, allocation-free loops.
	if (p_name == SNAME("range")) {
		return false;
	}
	return !p_name.is_empty() && (Variant::has_utility_function(p_name) || GDScriptUtilityFunctions::function_exists(p_name));
}

StringName get_builtin_alias_owner_from_datatype(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_hard_type() || p_type.is_variant()) {
		return StringName();
	}

	if (p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type < Variant::VARIANT_MAX) {
		const StringName type_name = Variant::get_type_name(p_type.builtin_type);
		return type_name == SNAME("Variant") ? StringName() : type_name;
	}

	if (!p_type.native_type.is_empty() && ClassDB::class_exists(p_type.native_type) && ClassDB::is_class_exposed(p_type.native_type)) {
		return p_type.native_type;
	}

	return StringName();
}

bool is_supported_builtin_member_alias_target(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return false;
	}

	const Variant::Type builtin_type = GDScriptParser::get_builtin_type(p_owner);
	if (builtin_type < Variant::VARIANT_MAX) {
		if (p_property) {
			if (p_static) {
				return Variant::has_constant(builtin_type, p_name) || Variant::has_enum(builtin_type, p_name) || Variant::get_enum_for_enumeration(builtin_type, p_name) != StringName();
			}

			Callable::CallError err;
			Variant dummy;
			Variant::construct(builtin_type, dummy, nullptr, 0, err);
			if (err.error != Callable::CallError::CALL_OK) {
				return false;
			}

			List<PropertyInfo> properties;
			dummy.get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.name == p_name) {
					return true;
				}
			}
			return false;
		}

		if (!Variant::has_builtin_method(builtin_type, p_name)) {
			return false;
		}

		const MethodInfo method_info = Variant::get_builtin_method_info(builtin_type, p_name);
		return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0);
	}

	if (!ClassDB::class_exists(p_owner) || !ClassDB::is_class_exposed(p_owner)) {
		return false;
	}

	if (p_property) {
		if (ClassDB::has_property(p_owner, p_name)) {
			return true;
		}
		if (p_static) {
			if (ClassDB::has_enum(p_owner, p_name)) {
				return true;
			}
			bool valid = false;
			(void)ClassDB::get_integer_constant(p_owner, p_name, &valid);
			return valid;
		}
		return false;
	}

	MethodInfo method_info;
	if (!ClassDB::get_method_info(p_owner, p_name, &method_info)) {
		return false;
	}

	return p_static == ((method_info.flags & METHOD_FLAG_STATIC) != 0 || Engine::get_singleton()->has_singleton(p_owner));
}

void get_or_create_builtin_function_alias(ExportContext *p_context, const StringName &p_name) {
	if (p_context == nullptr || !is_supported_builtin_function_alias_target(p_name)) {
		return;
	}

	(void)p_context->get_or_create_builtin_function_alias(p_name);
}

void add_builtin_function_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::IDENTIFIER) {
		return;
	}

	const GDScriptParser::IdentifierNode *callee = static_cast<const GDScriptParser::IdentifierNode *>(p_call->callee);
	if (callee->source != GDScriptParser::IdentifierNode::UNDEFINED_SOURCE) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_function_alias(p_call->function_name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, callee, String(*alias));
}

void get_or_create_builtin_member_alias(ExportContext *p_context, const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_context == nullptr || !is_supported_builtin_member_alias_target(p_owner, p_name, p_static, p_property)) {
		return;
	}

	(void)p_context->get_or_create_builtin_member_alias(p_owner, p_name, p_static, p_property);
}

void add_builtin_method_alias_call_replacement(RewriteContext &r_context, const GDScriptParser::CallNode *p_call) {
	if (!r_context.options.obfuscate_builtin_names ||
			r_context.export_context == nullptr ||
			p_call == nullptr ||
			p_call->is_super ||
			p_call->function_name.is_empty() ||
			p_call->function_name == SNAME("new") ||
			p_call->callee == nullptr ||
			p_call->callee->type != GDScriptParser::Node::SUBSCRIPT) {
		return;
	}

	const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute || subscript->base == nullptr || subscript->attribute == nullptr) {
		return;
	}

	const GDScriptParser::DataType base_type = subscript->base->type_constraint;
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_call->function_name, is_static, false);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, subscript->attribute, String(*alias));
}

void add_builtin_property_alias_reference_replacement(RewriteContext &r_context, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_base == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType base_type = p_base->type_constraint;
	const StringName owner = get_builtin_alias_owner_from_datatype(base_type);
	if (owner.is_empty()) {
		return;
	}

	const bool is_static = base_type.is_meta_type;
	const StringName *alias = r_context.export_context->get_builtin_member_alias(owner, p_identifier->name, is_static, true);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

void add_builtin_class_alias_name_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (!r_context.options.obfuscate_builtin_names || r_context.export_context == nullptr || p_identifier == nullptr || !is_supported_builtin_class_alias_target(p_identifier->name)) {
		return;
	}

	const StringName *alias = r_context.export_context->get_builtin_class_alias(p_identifier->name);
	if (alias == nullptr) {
		return;
	}

	add_replacement(r_context, p_identifier, String(*alias));
}

namespace {

void collect_builtin_class_aliases_from_identifier(ExportContext *p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_context == nullptr || p_identifier == nullptr || p_identifier->name.is_empty()) {
		return;
	}

	const GDScriptParser::DataType datatype = p_identifier->type_constraint;
	if (p_identifier->source == GDScriptParser::IdentifierNode::NATIVE_CLASS) {
		get_or_create_builtin_class_alias(p_context, !datatype.native_type.is_empty() ? datatype.native_type : p_identifier->name);
	} else if (p_identifier->source == GDScriptParser::IdentifierNode::UNDEFINED_SOURCE && datatype.is_meta_type) {
		if (datatype.kind == GDScriptParser::DataType::BUILTIN) {
			get_or_create_builtin_class_alias(p_context, Variant::get_type_name(datatype.builtin_type));
		} else if (datatype.kind == GDScriptParser::DataType::NATIVE) {
			get_or_create_builtin_class_alias(p_context, datatype.native_type);
		}
	}
}

class BuiltinAliasPlanner : public ExportASTVisitor {
	ExportContext &artifacts;

protected:
	bool enter(const GDScriptParser::Node *p_node, const ExportScope &p_scope) override {
		using Parser = GDScriptParser;
		switch (p_node->type) {
			case Parser::Node::CLASS:
				for (const auto *identifier : static_cast<const Parser::ClassNode *>(p_node)->extends) {
					get_or_create_builtin_class_alias(&artifacts, identifier->name);
				}
				break;
			case Parser::Node::TYPE: {
				const auto *node = static_cast<const Parser::TypeNode *>(p_node);
				if (!node->type_chain.is_empty()) {
					get_or_create_builtin_class_alias(&artifacts, node->type_chain[0]->name);
				}
			} break;
			case Parser::Node::IDENTIFIER:
				collect_builtin_class_aliases_from_identifier(&artifacts, static_cast<const Parser::IdentifierNode *>(p_node));
				break;
			case Parser::Node::SUBSCRIPT: {
				const auto *node = static_cast<const Parser::SubscriptNode *>(p_node);
				if (node->is_attribute) {
					const auto &base = node->base->type_constraint;
					get_or_create_builtin_member_alias(&artifacts, get_builtin_alias_owner_from_datatype(base), node->attribute->name, base.is_meta_type, true);
				}
			} break;
			case Parser::Node::CALL: {
				const auto *node = static_cast<const Parser::CallNode *>(p_node);
				if (node->is_super) {
					break;
				}
				if (node->get_callee_type() == Parser::Node::IDENTIFIER) {
					const auto *callee = static_cast<const Parser::IdentifierNode *>(node->callee);
					if (callee->source == Parser::IdentifierNode::UNDEFINED_SOURCE) {
						get_or_create_builtin_function_alias(&artifacts, node->function_name);
					}
				} else if (node->get_callee_type() == Parser::Node::SUBSCRIPT && node->function_name != SNAME("new")) {
					const auto *callee = static_cast<const Parser::SubscriptNode *>(node->callee);
					if (callee->is_attribute) {
						const auto &base = callee->base->type_constraint;
						get_or_create_builtin_member_alias(&artifacts, get_builtin_alias_owner_from_datatype(base), node->function_name, base.is_meta_type, false);
					}
				}
			} break;
			default:
				break;
		}
		return true;
	}

public:
	explicit BuiltinAliasPlanner(ExportContext &p_artifacts) :
			artifacts(p_artifacts) {}
};
} // namespace

void collect_builtin_class_aliases_from_node(ExportContext *p_context, const GDScriptParser::Node *p_node) {
	BuiltinAliasPlanner planner(*p_context);
	planner.walk(p_node);
}

} // namespace WGodotGDScriptExportTransform
