// wgodot-changes::file

#include "export_pass_diagnostics.h"

#include "export_ast_visitor.h"
#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

namespace {
using Parser = GDScriptParser;

bool is_literal_string(const Parser::ExpressionNode *p_expression) {
	if (p_expression->type == Parser::Node::LITERAL) {
		return static_cast<const Parser::LiteralNode *>(p_expression)->value.get_type() == Variant::STRING;
	}
	if (p_expression->type != Parser::Node::BINARY_OPERATOR) {
		return false;
	}
	const auto *binary = static_cast<const Parser::BinaryOpNode *>(p_expression);
	return binary->operation == Parser::BinaryOpNode::OP_ADDITION && is_literal_string(binary->left_operand) && is_literal_string(binary->right_operand);
}

class DiagnosticCalls : public ExportASTVisitor {
protected:
	bool enter(const Parser::Node *p_node, const ExportScope &p_scope) override {
		if (p_node->type != Parser::Node::CALL) {
			return true;
		}
		const auto *call = static_cast<const Parser::CallNode *>(p_node);
		if (call->is_super || call->get_callee_type() != Parser::Node::IDENTIFIER || call->arguments.size() != 1) {
			return true;
		}
		if (call->function_name != SNAME("push_error") && call->function_name != SNAME("push_warning") && call->function_name != SNAME("printerr")) {
			return true;
		}
		const auto *callee = static_cast<const Parser::IdentifierNode *>(call->callee);
		if (callee->source == Parser::IdentifierNode::UNDEFINED_SOURCE && is_literal_string(call->arguments[0])) {
			calls.push_back(call);
			return false;
		}
		return true;
	}

public:
	Vector<const Parser::CallNode *> calls;
};
} // namespace

Error DiagnosticPass::prescan(const ExportAnalysisInput &p_original, String &r_error) {
	Error error = p_original.analysis.analyze_scripts(true, r_error);
	if (error != OK) {
		return error;
	}
	for (const String &path : p_original.project.get_script_paths()) {
		if (!p_original.project.is_exported(path)) {
			continue;
		}
		Ref<GDScriptParserRef> parser = p_original.analysis.get_parser(path, GDScriptParserRef::FULLY_SOLVED, error);
		DiagnosticCalls visitor;
		visitor.walk(parser->get_parser()->get_tree());
		RewriteContext positions;
		positions.source = p_original.project.get_source(path)->get_text();
		build_line_offsets(positions);
		for (const auto *call : visitor.calls) {
			original_calls[path].insert(get_offset(positions, call->start_line, call->start_column));
		}
	}
	return OK;
}

Error DiagnosticPass::analyze(const ExportAnalysisInput &p_input, String &r_error) {
	Error error = p_input.analysis.analyze_scripts(true, r_error);
	if (error != OK) {
		return error;
	}
	current_calls.clear();
	for (const String &path : p_input.project.get_script_paths()) {
		const HashSet<int> *eligible = original_calls.getptr(path);
		if (eligible == nullptr) {
			continue;
		}
		const ExportSource &source = *p_input.project.get_source(path);
		Ref<GDScriptParserRef> parser = p_input.analysis.get_parser(path, GDScriptParserRef::FULLY_SOLVED, error);
		DiagnosticCalls visitor;
		visitor.walk(parser->get_parser()->get_tree());
		RewriteContext positions;
		positions.source = source.get_text();
		build_line_offsets(positions);
		for (const auto *call : visitor.calls) {
			const int offset = get_offset(positions, call->start_line, call->start_column);
			if (!eligible->has(source.get_origin(offset).original_offset)) {
				continue;
			}
			Call facts;
			const auto *argument = call->arguments[0];
			facts.start = get_offset(positions, argument->start_line, argument->start_column);
			facts.end = get_offset(positions, argument->end_line, argument->end_column);
			facts.original_line = source.get_original_line(offset);
			facts.function = call->function_name;
			facts.message = argument->reduced_value;
			current_calls[path].push_back(facts);
		}
	}
	return OK;
}

Error DiagnosticPass::transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) {
	for (const String &path : p_input.project.get_script_paths()) {
		const Vector<Call> *calls = current_calls.getptr(path);
		if (calls == nullptr) {
			continue;
		}
		for (const Call &call : *calls) {
			const String id = r_output.artifacts.get_diagnostics().add_message(path, call.original_line, call.function, call.message);
			SourceEdit edit;
			edit.start = call.start;
			edit.end = call.end;
			edit.text = "\"[" + id + "]\"";
			r_output.edits[path].push_back(edit);
		}
	}
	return OK;
}

} // namespace WGodotGDScriptExportTransform
