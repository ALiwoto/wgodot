// wgodot-changes::file
#include "wgodot_cpp_ast.h"
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include <functional>

using Parser = GDScriptParser;
using namespace WGodotCppNames;

namespace {
bool is_container(const Parser::DataType &p_type) {
	return p_type.kind == Parser::DataType::BUILTIN && !p_type.is_meta_type &&
			(p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY ||
					(p_type.builtin_type >= Variant::PACKED_BYTE_ARRAY && p_type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY));
}

String constant_name(const Parser::ConstantNode *p_constant) {
	// Locals with the same name in different functions/scopes are distinct.
	return "constant_" + symbol(p_constant->identifier->name) + "_" + itos(p_constant->start_line) + "_" + itos(p_constant->start_column);
}

class ConstantVisitor : public WGodotCppAstVisitor {
	std::function<bool(const Parser::Node *)> callback;
	bool visit(const Parser::Node *p_node) override { return true; }
	bool descend(const Parser::Node *p_node) override { return callback(p_node); }

public:
	explicit ConstantVisitor(std::function<bool(const Parser::Node *)> p_callback) : callback(std::move(p_callback)) {}
};
} // namespace

Parser::DataType WGodotCppEmitter::container_constant_type(const Parser::ConstantNode *p_constant) const {
	// Even an explicitly Variant-typed constant has a known immutable type.
	return p_constant->type_constraint.is_variant() ? p_constant->initializer->type_constraint : p_constant->type_constraint;
}

const Parser::ConstantNode *WGodotCppEmitter::container_constant_source(const Parser::ExpressionNode *p_expression) const {
	const Parser::ConstantNode *constant = nullptr;
	if (p_expression->type == Parser::Node::IDENTIFIER) {
		const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_expression);
		if (identifier->source == Parser::IdentifierNode::LOCAL_CONSTANT || identifier->source == Parser::IdentifierNode::MEMBER_CONSTANT) {
			constant = identifier->constant_source;
		}
	} else if (p_expression->type == Parser::Node::SUBSCRIPT) {
		const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && subscript->base->type_constraint.kind == Parser::DataType::CLASS) {
			const auto *owner = member_owner(subscript->base->type_constraint.class_type, subscript->attribute->name);
			if (owner) {
				const auto &member = owner->node->get_member(subscript->attribute->name);
				if (member.type == Parser::ClassNode::Member::CONSTANT) {
					constant = member.constant;
				}
			}
		}
	}
	return container_constant_owners.has(constant) ? constant : nullptr;
}

bool WGodotCppEmitter::can_inline_constant(const Parser::ExpressionNode *p_expression) const {
	if (!p_expression->is_constant || !p_expression->reduced || p_expression->type == Parser::Node::ARRAY || p_expression->type == Parser::Node::DICTIONARY || container_constant_source(p_expression)) {
		return false;
	}
	// Rebuild container expressions from their operands during initialization:
	// folding their contents would lose aliases to other constant containers.
	// Scalar results continue to use the analyzer's reduced value directly.
	return !is_container(expression_type(p_expression));
}

void WGodotCppEmitter::collect_container_constants() {
	container_constant_owners.clear();
	class_container_constants.clear();
	used_container_constants.clear();
	for (const auto &entry : project.get_classes()) {
		ConstantVisitor declarations([&](const Parser::Node *p_node) {
			if (p_node->type == Parser::Node::CLASS && p_node != entry.node) {
				return false;
			}
			if (p_node->type == Parser::Node::CONSTANT) {
				const auto *constant = static_cast<const Parser::ConstantNode *>(p_node);
				if (is_container(container_constant_type(constant))) {
					container_constant_owners.insert(constant, &entry);
					class_container_constants[entry.node].push_back(constant);
				}
			}
			return true;
		});
		declarations.walk(entry.node);
	}

	Vector<const Parser::ConstantNode *> pending;
	ConstantVisitor uses([&](const Parser::Node *p_node) {
		if (p_node->type == Parser::Node::CONSTANT) {
			return false; // Initializers are visited when the container is needed.
		}
		if (p_node->is_expression()) {
			const auto *expression = static_cast<const Parser::ExpressionNode *>(p_node);
			if (can_inline_constant(expression)) {
				return false;
			}
			if (const auto *constant = container_constant_source(expression)) {
				if (!used_container_constants.has(constant)) {
					used_container_constants.insert(constant);
					pending.push_back(constant);
				}
				return false;
			}
		}
		return true;
	});
	for (const auto &entry : project.get_classes()) {
		if (!entry.node->outer) {
			uses.walk(entry.node);
		}
	}
	for (int i = 0; i < pending.size(); i++) {
		uses.walk(pending[i]->initializer);
	}
}

WGodotCppEmitter::Value WGodotCppEmitter::container_constant(const Parser::ConstantNode *p_constant) {
	const auto *owner = container_constant_owners[p_constant];
	class_dependencies.insert(owner->cpp_name);
	const auto datatype = container_constant_type(p_constant);
	Value value(owner->cpp_name + "::" + constant_name(p_constant) + "()", type(datatype, p_constant));
	// The first access allocates storage and may load resources. Copies of the
	// returned handle share that storage, including its read-only flag.
	value.effects = true;
	value.read_only = datatype.builtin_type == Variant::ARRAY || datatype.builtin_type == Variant::DICTIONARY;
	return value;
}

String WGodotCppEmitter::resource_load(const String &p_path) {
	class_call_headers.insert("core/io/resource_loader.h");
	return "::ResourceLoader::load(String::utf8(" + quoted(p_path) + "))";
}

void WGodotCppEmitter::emit_container_constants(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions) {
	const auto *constants = class_container_constants.getptr(p_class.node);
	if (!constants) {
		return;
	}
	for (const auto *constant : *constants) {
		if (!used_container_constants.has(constant)) {
			continue;
		}
		current_function = nullptr;
		function_failed = false;
		initializing_container_constant = true;
		const auto datatype = container_constant_type(constant);
		const String value_type = type(datatype, constant);
		Value value = lower_converted(constant->initializer, datatype, constant);
		initializing_container_constant = false;
		if (!value.read_only && (datatype.builtin_type == Variant::ARRAY || datatype.builtin_type == Variant::DICTIONARY)) {
			Vector<String> setup;
			materialize(value, setup);
			setup.push_back(value.code + ".make_read_only();");
			value.setup = setup;
		}
		class_call_headers.insert("modules/wgodot/native/wgodot_native_static.h");
		const String name = constant_name(constant);
		r_declaration += "\tstatic " + value_type + " " + name + "();\n";
		// Direct member initialization avoids allocating an empty container only
		// to replace it. Constant dependencies are acyclic, unlike static fields.
		r_definitions += value_type + " " + p_class.cpp_name + "::" + name + "() {\n\tstruct Storage {\n\t\t" + value_type + " value = " + value.expression().replace("\n", "\n\t\t") + ";\n\t};\n\treturn WGodotNative::StaticStorage<Storage>::get().value;\n}\n\n";
	}
}
