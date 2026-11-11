// wgodot-changes::file
#include "gdscript_parser.h"

GDScriptParser::ExpressionNode *GDScriptParser::wgodot_parse_field_initializers(ExpressionNode *p_previous_operand, bool p_can_assign) {
	LocalVector<CallNode::FieldInitializer> fields;
	while (!check(GDScriptTokenizer::Token::BRACE_CLOSE) && !is_at_end()) {
		if (!consume(GDScriptTokenizer::Token::IDENTIFIER, "Expected a field name in the construction initializer.")) {
			break;
		}
		CallNode::FieldInitializer field;
		field.name = parse_identifier();
		if (!consume(GDScriptTokenizer::Token::COLON, "Expected ':' after the field name.")) {
			break;
		}
		field.value = parse_expression(false);
		if (!field.value) {
			push_error("Expected a field initialization expression.");
			break;
		}
		fields.push_back(field);
		if (!match(GDScriptTokenizer::Token::COMMA)) {
			break;
		}
	}
	pop_multiline();
	if (!consume(GDScriptTokenizer::Token::BRACE_CLOSE, "Expected '}' after field initializers.") ||
			!consume(GDScriptTokenizer::Token::PERIOD, "Field initializers must be followed by '.new(...)'.") ||
			!consume(GDScriptTokenizer::Token::IDENTIFIER, "Expected 'new' after field initializers.")) {
		return nullptr;
	}
	IdentifierNode *method = parse_identifier();
	if (method->name != SNAME("new")) {
		push_error("Field initializers must be followed by '.new(...)'.", method);
		return nullptr;
	}
	SubscriptNode *callee = alloc_node<SubscriptNode>();
	callee->base = p_previous_operand;
	callee->attribute = method;
	callee->is_attribute = true;
	reset_extents(callee, p_previous_operand);
	complete_extents(callee);
	push_multiline(true);
	if (!consume(GDScriptTokenizer::Token::PARENTHESIS_OPEN, "Expected '(' after 'new'.")) {
		pop_multiline();
		return nullptr;
	}
	ExpressionNode *expression = parse_call(callee, p_can_assign);
	if (expression) {
		CallNode *call = static_cast<CallNode *>(expression);
		call->wgodot_has_field_initializers = true;
		call->wgodot_field_initializers = fields;
	}
	return expression;
}
