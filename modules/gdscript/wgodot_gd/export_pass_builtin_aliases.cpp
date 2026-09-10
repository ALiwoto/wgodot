// wgodot-changes::file

#include "../gdscript_tokenizer.h"
#include "export_ast_visitor.h"
#include "export_pass_ast.h"
#include "export_transform_internal.h"
#include "name_obfuscation.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

class BuiltinAliasesVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;

protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		rewrite.current_class = p_scope.current_class;
		switch (p_node->type) {
			case Parser::Node::CLASS:
				for (const auto *identifier : static_cast<const Parser::ClassNode *>(p_node)->extends) {
					add_builtin_class_alias_name_replacement(rewrite, identifier);
				}
				break;
			case Parser::Node::TYPE: {
				const auto *node = static_cast<const Parser::TypeNode *>(p_node);
				if (!node->type_chain.is_empty()) {
					add_builtin_class_alias_name_replacement(rewrite, node->type_chain[0]);
				}
			} break;
			case Parser::Node::IDENTIFIER:
				add_builtin_class_alias_reference_replacement(rewrite, static_cast<const Parser::IdentifierNode *>(p_node));
				break;
			case Parser::Node::CALL: {
				const auto *node = static_cast<const Parser::CallNode *>(p_node);
				add_builtin_function_alias_call_replacement(rewrite, node);
				add_builtin_method_alias_call_replacement(rewrite, node);
			} break;
			case Parser::Node::SUBSCRIPT: {
				const auto *node = static_cast<const Parser::SubscriptNode *>(p_node);
				if (node->is_attribute) {
					add_builtin_property_alias_reference_replacement(rewrite, node->base, node->attribute);
				}
			} break;
			default:
				break;
		}
		return true;
	}

public:
	explicit BuiltinAliasesVisitor(RewriteContext &p_rewrite) : rewrite(p_rewrite) {}
};
} // namespace

Error BuiltinAliasesPass::analyze(const ExportAnalysisInput &p_input, String &r_error) {
	Error error = AnalyzedExportPass::analyze(p_input, r_error);
	if (error != OK) {
		return error;
	}
	for (const String &path : p_input.project.get_script_paths()) {
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(p_input.project.get_source(path)->get_text());
		for (auto token = tokenizer.scan(); token.type != GDScriptTokenizer::Token::TK_EOF; token = tokenizer.scan()) {
			if (token.type == GDScriptTokenizer::Token::IDENTIFIER) {
				identifiers.insert(StringName(token.literal));
			}
		}
	}
	return OK;
}

Error BuiltinAliasesPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const StringName &identifier : identifiers) {
		r_output.artifacts.reserve_global_class_name(identifier);
	}
	for (const String &path : p_input.project.get_script_paths()) {
		if (const auto *parser = analyzed_scripts.getptr(path)) {
			collect_builtin_class_aliases_from_node(&r_output.artifacts, (*parser)->get_parser()->get_tree());
		}
	}
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.obfuscate_builtin_names = true;
		BuiltinAliasesVisitor visitor(rewrite);
		visitor.walk((*parser)->get_parser()->get_tree());
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
