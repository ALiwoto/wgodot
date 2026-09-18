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
			case Parser::Node::BINARY_OPERATOR:
				return !add_string_concat_replacement(rewrite, static_cast<const Parser::BinaryOpNode *>(p_node));
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
