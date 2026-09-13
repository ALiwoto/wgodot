// wgodot-changes::file

#include "deconst_transform.h"
#include "export_ast_visitor.h"
#include "export_pass_ast.h"
#include "export_transform_internal.h"
#include "name_obfuscation.h"

#include "modules/gdscript/gdscript_analyzer.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

class ReplacedPathConstants : public ExportASTVisitor {
	HashMap<const Parser::ConstantNode *, int> remaining_usages;
	Vector<const Parser::ConstantNode *> unused;

protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		if (p_node->type == Parser::Node::IDENTIFIER) {
			const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_node);
			if (identifier->source == Parser::IdentifierNode::LOCAL_CONSTANT) {
				const auto *constant = identifier->constant_source;
				if (!remaining_usages.has(constant)) {
					remaining_usages.insert(constant, constant->usages);
				}
				if (--remaining_usages[constant] == 0) {
					unused.push_back(constant);
					walk_child(constant->initializer, p_scope);
				}
			}
		}
		return true;
	}

public:
	void remove_unused(RewriteContext &r_rewrite) const {
		// Replacing a constant's last use must not introduce an unused-constant error.
		for (const auto *constant : unused) {
			add_constant_declaration_replacement(r_rewrite, constant, false);
			const Replacement removal = r_rewrite.replacements[r_rewrite.replacements.size() - 1];
			for (int i = r_rewrite.replacements.size() - 2; i >= 0; i--) {
				const Replacement &replacement = r_rewrite.replacements[i];
				if (replacement.start >= removal.start && replacement.end <= removal.end) {
					r_rewrite.replacements.remove_at(i);
				}
			}
		}
	}
};

class NamesVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;
	const GDScriptAnalyzer &analyzer;
	ReplacedPathConstants replaced_path_constants;

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
				const bool path_replaced = add_tween_property_path_replacement(rewrite, node, analyzer.wgodot_get_tween_property_path(node));
				if (path_replaced) {
					replaced_path_constants.walk(node->arguments[1]);
				}
				const int count = rewrite.replacements.size();
				add_call_member_name_reference_replacement(rewrite, node);
				const bool callee_replaced = rewrite.replacements.size() != count;
				if (callee_replaced || path_replaced) {
					if (!callee_replaced) {
						walk_child(node->callee, p_scope);
					} else if (node->callee->type == Parser::Node::SUBSCRIPT) {
						walk_child(static_cast<const Parser::SubscriptNode *>(node->callee)->base, p_scope);
					}
					for (uint32_t i = 0; i < node->arguments.size(); i++) {
						if (i != 1 || !path_replaced) {
							walk_child(node->arguments[i], p_scope);
						}
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
	void remove_unused_path_constants() { replaced_path_constants.remove_unused(rewrite); }

	NamesVisitor(RewriteContext &p_rewrite, const GDScriptAnalyzer &p_analyzer) :
			rewrite(p_rewrite), analyzer(p_analyzer) {}
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
		NamesVisitor visitor(rewrite, *(*parser)->get_analyzer());
		visitor.walk(tree);
		visitor.remove_unused_path_constants();
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
