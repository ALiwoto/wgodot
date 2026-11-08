// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_gd/interface_helpers.h"

void GDScriptAnalyzer::wgodot_add_serialization_factories(GDScriptParser::ClassNode *p_class) {
	using P = GDScriptParser;
	const bool json = WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "JsonSerializable");
	const bool binary = WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "BinarySerializable");
	const bool protobuf = WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "ProtobufSerializable");
	if ((!json && !binary && !protobuf) || p_class->is_abstract || p_class->wgodot_is_interface) {
		return;
	}
	if (!p_class->identifier) {
		push_error("Serializable classes need a class_name or a named inner class.", p_class);
		return;
	}
	const char *names[] = { "deserialize_json", "deserialize_json_dict", "deserialize_binary", "deserialize_protobuf" };
	const char *types[] = { "String", "Dictionary", "PackedByteArray", "PackedByteArray" };
	for (int index = 0; index < 4; ++index) {
		if ((index < 2 && !json) || (index == 2 && !binary) || (index == 3 && !protobuf)) {
			continue;
		}
		const StringName name = names[index];
		if (p_class->has_member(name)) {
			continue;
		}
		auto identifier = [&](const StringName &p_name, P::SuiteNode *p_suite = nullptr) {
			auto *node = parser->alloc_recovery_node<P::IdentifierNode>();
			node->name = p_name;
			node->suite = p_suite;
			if (p_suite && p_suite->has_local(p_name)) {
				const auto &local = p_suite->get_local(p_name);
				node->source_function = local.source_function;
				if (local.type == P::SuiteNode::Local::PARAMETER) {
					node->source = P::IdentifierNode::FUNCTION_PARAMETER;
					node->parameter_source = local.parameter;
				} else if (local.type == P::SuiteNode::Local::VARIABLE) {
					node->source = P::IdentifierNode::LOCAL_VARIABLE;
					node->variable_source = local.variable;
				}
			}
			return node;
		};
		auto datatype = [&](const StringName &p_name) {
			auto *node = parser->alloc_recovery_node<P::TypeNode>();
			node->type_chain.push_back(identifier(p_name));
			return node;
		};
		auto call = [&](P::ExpressionNode *p_base, const StringName &p_name) {
			auto *subscript = parser->alloc_recovery_node<P::SubscriptNode>();
			subscript->base = p_base;
			subscript->is_attribute = true;
			subscript->attribute = identifier(p_name);
			auto *node = parser->alloc_recovery_node<P::CallNode>();
			node->callee = subscript;
			node->function_name = p_name;
			return node;
		};
		auto *function = parser->alloc_recovery_node<P::FunctionNode>();
		function->wgodot_serialization_factory = index + 1;
		function->identifier = identifier(name);
		function->is_static = true;
		function->return_type = datatype(p_class->identifier->name);
		function->body = parser->alloc_recovery_node<P::SuiteNode>();
		function->body->parent_function = function;
		function->body->has_return = true;
		auto *parameter = parser->alloc_recovery_node<P::ParameterNode>();
		parameter->identifier = identifier("data", function->body);
		parameter->datatype_specifier = datatype(types[index]);
		parameter->wgodot_engine_boundary = index == 1;
		parameter->datatype_specifier->wgodot_engine_boundary = index == 1;
		function->parameters.push_back(parameter);
		function->parameters_indices["data"] = 0;
		function->body->add_local(parameter, function);
		auto *value = parser->alloc_recovery_node<P::VariableNode>();
		value->identifier = identifier("decoded_model", function->body);
		value->datatype_specifier = datatype(p_class->identifier->name);
		value->initializer = call(identifier(p_class->identifier->name, function->body), "new");
		function->body->statements.push_back(value);
		function->body->add_local(value, function);
		auto *decode = call(identifier("decoded_model", function->body), String("_") + names[index]);
		decode->arguments.push_back(identifier("data", function->body));
		auto *condition = parser->alloc_recovery_node<P::IfNode>();
		condition->condition = decode;
		condition->true_block = parser->alloc_recovery_node<P::SuiteNode>();
		condition->true_block->parent_function = function;
		condition->true_block->parent_block = function->body;
		condition->true_block->parent_if = condition;
		condition->true_block->has_return = true;
		auto *success = parser->alloc_recovery_node<P::ReturnNode>();
		success->return_value = identifier("decoded_model", condition->true_block);
		condition->true_block->statements.push_back(success);
		function->body->statements.push_back(condition);
		auto *failure = parser->alloc_recovery_node<P::ReturnNode>();
		failure->return_value = parser->alloc_recovery_node<P::LiteralNode>();
		function->body->statements.push_back(failure);
		p_class->add_member(function);
	}
}

void GDScriptAnalyzer::wgodot_resolve_serialization_fields(GDScriptParser::ClassNode *p_class) {
	using P = GDScriptParser;
	if (!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "JsonSerializable") &&
			!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "BinarySerializable") &&
			!WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, "ProtobufSerializable")) {
		return;
	}
	const auto resolve = [&](const auto &self, const P::DataType &p_type, const P::Node *p_source) -> void {
		if (p_type.kind == P::DataType::CLASS) {
			resolve_class_interface(p_type.class_type, p_source);
		}
		for (int i = 0; i < p_type.get_container_element_type_count(); i++) {
			self(self, p_type.get_container_element_type(i), p_source);
		}
	};
	for (const auto &member : p_class->members) {
		if (member.type == P::ClassNode::Member::VARIABLE && !member.variable->is_static) {
			resolve(resolve, member.variable->type_constraint, member.variable);
		}
	}
}
