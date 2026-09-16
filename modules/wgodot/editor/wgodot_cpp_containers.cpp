// wgodot-changes::file
#include "wgodot_cpp_ast.h"
#include "wgodot_cpp_project.h"

using Parser = GDScriptParser;

namespace {

// Packed-array and dictionary ownership is still undecided. Typed Arrays use
// WArray; their conversions and engine boundaries are checked by the emitter.
class ContainerSharing : public WGodotCppAstVisitor {
	const String &path;
	Vector<String> &diagnostics;

	bool references_storage(const Parser::ExpressionNode *p_value, bool p_return) const {
		if (!p_value || p_value->type_constraint.kind != Parser::DataType::BUILTIN) {
			return false;
		}
		const Variant::Type type = p_value->type_constraint.builtin_type;
		if (type != Variant::DICTIONARY && !(type >= Variant::PACKED_BYTE_ARRAY && type <= Variant::PACKED_VECTOR4_ARRAY)) {
			return false;
		}
		switch (p_value->type) {
			case Parser::Node::IDENTIFIER: {
				const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_value);
				// Returning a locally built container can transfer its value. Copies
				// into another storage slot are rejected at their assignment site.
				return !p_return || identifier->source != Parser::IdentifierNode::LOCAL_VARIABLE;
			}
			case Parser::Node::SUBSCRIPT:
				return true;
			case Parser::Node::CAST:
				return references_storage(static_cast<const Parser::CastNode *>(p_value)->operand, p_return);
			case Parser::Node::TERNARY_OPERATOR: {
				const auto *ternary = static_cast<const Parser::TernaryOpNode *>(p_value);
				return references_storage(ternary->true_expr, p_return) || references_storage(ternary->false_expr, p_return);
			}
			default:
				return false;
		}
	}

	bool visit(const Parser::Node *p_node) override {
		const Parser::ExpressionNode *value = nullptr;
		bool returning = false;
		switch (p_node->type) {
			case Parser::Node::VARIABLE:
				value = static_cast<const Parser::VariableNode *>(p_node)->initializer;
				break;
			case Parser::Node::CONSTANT:
				value = static_cast<const Parser::ConstantNode *>(p_node)->initializer;
				break;
			case Parser::Node::ASSIGNMENT: {
				const auto *assignment = static_cast<const Parser::AssignmentNode *>(p_node);
				if (assignment->operation == Parser::AssignmentNode::OP_NONE) {
					value = assignment->assigned_value;
				}
				break;
			}
			case Parser::Node::RETURN:
				value = static_cast<const Parser::ReturnNode *>(p_node)->return_value;
				returning = true;
				break;
			default:
				break;
		}
		if (references_storage(value, returning)) {
			diagnostics.push_back(vformat("%s:%d:%d: Native export does not yet support shared container %s (%s). This retains existing storage; native container ownership must be defined before this can be translated", path, value->start_line, value->start_column, returning ? "returns" : "assignments", value->type_constraint.to_string()));
		}
		return true;
	}

public:
	ContainerSharing(const String &p_path, Vector<String> &r_diagnostics) : path(p_path), diagnostics(r_diagnostics) {}
};

} // namespace

void WGodotCppProject::validate_container_sharing(const String &p_script_path, const Parser::ClassNode *p_class) {
	ContainerSharing check(p_script_path, diagnostics);
	check.walk(p_class);
}
