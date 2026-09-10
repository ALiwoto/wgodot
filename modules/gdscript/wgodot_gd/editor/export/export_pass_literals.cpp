// wgodot-changes::file

#include "export_ast_visitor.h"
#include "export_pass_ast.h"
#include "export_transform_internal.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

class LiteralsVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;

protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		rewrite.current_class = p_scope.current_class;
		rewrite.no_string_mangle_scope = p_scope.no_string_mangle;
		switch (p_node->type) {
			case Parser::Node::CLASS:
				add_extends_path_replacement(rewrite, static_cast<const Parser::ClassNode *>(p_node));
				break;
			case Parser::Node::LITERAL:
				add_string_literal_replacement(rewrite, static_cast<const Parser::LiteralNode *>(p_node));
				break;
			case Parser::Node::BINARY_OPERATOR: {
				const auto *node = static_cast<const Parser::BinaryOpNode *>(p_node);
				if (rewrite.options.obfuscate_strings) {
					return !add_string_concat_replacement(rewrite, node);
				}
				if (node->operation == Parser::BinaryOpNode::OP_ADDITION && node->is_constant && node->reduced_value.get_type() == Variant::STRING) {
					const String replacement = get_export_string_literal_replacement(rewrite, Variant::STRING, node->reduced_value);
					if (!replacement.is_empty()) {
						add_replacement(rewrite, node, replacement);
						return false;
					}
				}
			} break;
			default:
				break;
		}
		return true;
	}

public:
	explicit LiteralsVisitor(RewriteContext &p_rewrite) :
			rewrite(p_rewrite) {}
};
} // namespace

Error PathsPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_exported_paths()) {
		r_output.artifacts.reserve_script_path(path);
	}
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser != nullptr && has_obfuscate_path_annotation((*parser)->get_parser()->get_tree())) {
			(void)r_output.artifacts.get_or_create_script_path_rename(path);
		}
	}
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.obfuscate_file_paths = true;
		LiteralsVisitor visitor(rewrite);
		visitor.walk((*parser)->get_parser()->get_tree());
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

Error StringsPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.obfuscate_strings = true;
		LiteralsVisitor visitor(rewrite);
		visitor.walk((*parser)->get_parser()->get_tree());
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
