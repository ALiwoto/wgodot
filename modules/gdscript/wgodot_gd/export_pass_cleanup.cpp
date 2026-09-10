// wgodot-changes::file

#include "export_ast_visitor.h"
#include "export_pass_ast.h"
#include "export_transform_internal.h"

namespace WGodotGDScriptExportTransform {

namespace {
class CleanupVisitor : public ExportASTVisitor {
	RewriteContext &rewrite;

protected:
	bool enter(const GDScriptParser::Node *p_node, const ExportScope &p_scope) override {
		if (p_node->type == GDScriptParser::Node::ANNOTATION) {
			const auto *annotation = static_cast<const GDScriptParser::AnnotationNode *>(p_node);
			if (should_strip_export_annotation(annotation)) {
				add_annotation_strip_replacement(rewrite, annotation);
				return false;
			}
		}
		return true;
	}

public:
	explicit CleanupVisitor(RewriteContext &p_rewrite) : rewrite(p_rewrite) {}
};
} // namespace

Error CleanupPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_script_paths()) {
		const auto *parser = analyzed_scripts.getptr(path);
		if (parser == nullptr) {
			continue;
		}
		RewriteContext rewrite;
		setup_rewrite(p_input, r_output, path, rewrite);
		rewrite.options.strip_comments = p_input.get_options().strip_comments;
		rewrite.options.strip_empty_lines = p_input.get_options().strip_empty_lines;
		CleanupVisitor visitor(rewrite);
		visitor.walk((*parser)->get_parser()->get_tree());
		collect_comment_replacements(rewrite, *(*parser)->get_parser());
		collect_empty_line_replacements(rewrite);
		finish_rewrite(path, rewrite, r_output);
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
