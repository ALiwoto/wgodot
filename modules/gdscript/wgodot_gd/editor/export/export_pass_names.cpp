// wgodot-changes::file

#include "export_ast_visitor.h"
#include "export_pass_ast.h"
#include "export_transform_internal.h"
#include "name_obfuscation.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

class NamesVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;

protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		rewrite.current_class = p_scope.current_class;
		switch (p_node->type) {
			case Parser::Node::CLASS: {
				const auto *node = static_cast<const Parser::ClassNode *>(p_node);
				add_class_declaration_name_replacement(rewrite, node);
				for (const auto *identifier : node->extends) {
					add_global_class_name_reference_replacement(rewrite, identifier);
				}
				for (const auto &contract : node->wgodot_implements) {
					for (const auto *identifier : contract.identifiers) {
						const int count = rewrite.replacements.size();
						add_global_class_name_reference_replacement(rewrite, identifier);
						if (rewrite.replacements.size() == count) {
							add_class_member_name_reference_replacement(rewrite, identifier);
						}
					}
				}
			} break;
			case Parser::Node::TYPE: {
				const auto *node = static_cast<const Parser::TypeNode *>(p_node);
				if (!node->type_chain.is_empty()) {
					if (node->type_chain.size() != 1 || !add_class_member_name_reference_replacement(rewrite, node->type_chain[0], node->resolved_type)) {
						add_global_class_name_reference_replacement(rewrite, node->type_chain[0]);
					}
				}
				for (uint32_t i = 1; i < node->type_chain.size(); i++) {
					add_class_member_name_reference_replacement(rewrite, node->type_chain[i]);
				}
			} break;
			case Parser::Node::IDENTIFIER: {
				const auto *node = static_cast<const Parser::IdentifierNode *>(p_node);
				const int count = rewrite.replacements.size();
				add_global_class_name_reference_replacement(rewrite, node);
				if (rewrite.replacements.size() == count && !add_member_name_reference_replacement(rewrite, node, p_scope.current_class) && !p_scope.no_mangle) {
					add_local_name_reference_replacement(rewrite, node);
				}
			} break;
			case Parser::Node::SUBSCRIPT: {
				const auto *node = static_cast<const Parser::SubscriptNode *>(p_node);
				if (node->is_attribute) {
					const int count = rewrite.replacements.size();
					add_global_class_name_reference_replacement(rewrite, node->attribute);
					if (rewrite.replacements.size() == count) {
						add_attribute_member_name_reference_replacement(rewrite, node->base, node->attribute);
					}
				}
			} break;
			case Parser::Node::CALL: {
				const auto *node = static_cast<const Parser::CallNode *>(p_node);
				const int count = rewrite.replacements.size();
				add_call_member_name_reference_replacement(rewrite, node);
				if (rewrite.replacements.size() != count) {
					if (node->callee->type == Parser::Node::SUBSCRIPT) {
						walk_child(static_cast<const Parser::SubscriptNode *>(node->callee)->base, p_scope);
					}
					for (const auto *argument : node->arguments) {
						walk_child(argument, p_scope);
					}
					return false;
				}
			} break;
			case Parser::Node::SUITE:
				if (!p_scope.no_mangle) {
					collect_suite_local_name_obfuscation(rewrite, static_cast<const Parser::SuiteNode *>(p_node));
				}
				break;
			case Parser::Node::SIGNAL:
				if (!p_scope.no_mangle) {
					add_signal_parameter_name_replacements(rewrite, static_cast<const Parser::SignalNode *>(p_node));
				}
				break;
			case Parser::Node::VARIABLE: {
				const auto *node = static_cast<const Parser::VariableNode *>(p_node);
				if (!p_scope.no_mangle && node->property == Parser::VariableNode::PROP_SETGET) {
					add_function_pointer_replacement(rewrite, p_scope.current_class, node->setter_pointer);
					add_function_pointer_replacement(rewrite, p_scope.current_class, node->getter_pointer);
				}
			} break;
			default:
				break;
		}
		return true;
	}

public:
	explicit NamesVisitor(RewriteContext &p_rewrite) :
			rewrite(p_rewrite) {}
};
} // namespace

Error NamesPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	Vector<GlobalClassRenameRequest> classes;
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		const auto *tree = (*parser)->get_parser()->get_tree();
		r_output.artifacts.reserve_script_member_names(tree);
		r_output.artifacts.reserve_script_declaration_names_for_global_classes(tree);
		collect_global_class_rename_request(&r_output.artifacts, tree, path, classes);
	}
	for (const auto &request : classes) {
		(void)r_output.artifacts.get_or_create_global_class_rename(request.name, request.path);
	}
	for (const String &path : p_input.project.get_script_paths()) {
		if (const auto *parser = analyzed_scripts.getptr(path)) {
			r_output.artifacts.index_interface_members((*parser)->get_parser()->get_tree());
		}
	}
	for (const String &path : p_input.project.get_script_paths()) {
		if (const auto *parser = analyzed_scripts.getptr(path)) {
			r_output.artifacts.index_script((*parser)->get_parser()->get_tree(), path);
		}
	}
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.obfuscate_names = true;
		const auto *tree = (*parser)->get_parser()->get_tree();
		r_output.artifacts.seed_reserved_obfuscated_names(rewrite.reserved_obfuscated_names);
		collect_member_name_obfuscation(rewrite, tree, false);
		NamesVisitor visitor(rewrite);
		visitor.walk(tree);
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
