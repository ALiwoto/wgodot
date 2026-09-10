// wgodot-changes::file

#include "deconst_transform.h"
#include "deenum_transform.h"
#include "export_ast_visitor.h"
#include "export_pass_ast.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

class ConstantsVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;
	HashSet<const Parser::Node *> leave_pass;

	bool removed(const Parser::Node *p_node) const {
		if (p_node->type == Parser::Node::CONSTANT) {
			return should_deconst_constant(rewrite, static_cast<const Parser::ConstantNode *>(p_node));
		}
		return p_node->type == Parser::Node::ENUM && should_deenum_enum(rewrite, static_cast<const Parser::EnumNode *>(p_node));
	}

protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		rewrite.current_class = p_scope.current_class;
		switch (p_node->type) {
			case Parser::Node::CLASS: {
				const auto *node = static_cast<const Parser::ClassNode *>(p_node);
				if (node->outer != nullptr && !node->members.is_empty()) {
					bool empty = true;
					for (const auto &member : node->members) {
						empty &= removed(member.type == Parser::ClassNode::Member::ENUM_VALUE ? member.enum_value.parent_enum : member.get_source_node());
					}
					if (empty) {
						const auto &first = node->members[0];
						leave_pass.insert(first.type == Parser::ClassNode::Member::ENUM_VALUE ? first.enum_value.parent_enum : first.get_source_node());
					}
				}
			} break;
			case Parser::Node::SUITE: {
				const auto *node = static_cast<const Parser::SuiteNode *>(p_node);
				bool empty = !node->statements.is_empty();
				for (const auto *statement : node->statements) {
					empty &= removed(statement);
				}
				if (empty) {
					leave_pass.insert(node->statements[0]);
				}
			} break;
			case Parser::Node::CONSTANT:
				if (removed(p_node)) {
					add_constant_declaration_replacement(rewrite, static_cast<const Parser::ConstantNode *>(p_node), leave_pass.has(p_node));
					return false;
				}
				break;
			case Parser::Node::ENUM:
				if (removed(p_node)) {
					add_enum_declaration_replacement(rewrite, static_cast<const Parser::EnumNode *>(p_node), leave_pass.has(p_node));
					return false;
				}
				break;
			case Parser::Node::TYPE:
				return !add_enum_type_replacement(rewrite, static_cast<const Parser::TypeNode *>(p_node));
			case Parser::Node::IDENTIFIER: {
				const auto *node = static_cast<const Parser::IdentifierNode *>(p_node);
				if (add_enum_identifier_reference_replacement(rewrite, node)) {
					return false;
				}
				if (is_declared_constant_identifier(rewrite, node)) {
					add_constant_reference_replacement(rewrite, node, node->constant_source);
				}
			} break;
			case Parser::Node::SUBSCRIPT: {
				const auto *node = static_cast<const Parser::SubscriptNode *>(p_node);
				if (add_enum_attribute_reference_replacement(rewrite, node)) {
					return false;
				}
				bool replaced_whole = false;
				if (add_constant_indexed_reference_replacement(rewrite, node, replaced_whole)) {
					if (!replaced_whole) {
						walk_child(node->index, p_scope);
					}
					return false;
				}
				if (node->is_attribute && is_declared_constant_identifier(rewrite, node->attribute)) {
					add_constant_reference_replacement(rewrite, node, node->attribute->constant_source);
					return false;
				}
			} break;
			default:
				break;
		}
		return true;
	}

public:
	explicit ConstantsVisitor(RewriteContext &p_rewrite) :
			rewrite(p_rewrite) {}
};
} // namespace

Error ConstantsPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.deconst_exports = true;
		ConstantsVisitor visitor(rewrite);
		visitor.walk((*parser)->get_parser()->get_tree());
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
