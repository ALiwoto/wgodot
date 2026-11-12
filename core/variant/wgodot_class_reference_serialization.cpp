// wgodot-changes::file
#include "wgodot_class_reference_serialization.h"

#include "core/object/wgodot_class_references.h"

namespace {
Error read_name(VariantParser::Stream *p_stream, StringName &r_name, int &r_line, String &r_error, bool p_constraint) {
	using Parser = VariantParser;
	Parser::Token token;
	RETURN_IF_ERROR(Parser::get_token(p_stream, token, r_line, r_error));
	if (token.type != (p_constraint ? Parser::TK_BRACKET_OPEN : Parser::TK_PARENTHESIS_OPEN)) {
		r_error = p_constraint ? "Expected '[' after ClassRef type." : "Expected '(' after ClassRef value.";
		return ERR_PARSE_ERROR;
	}
	RETURN_IF_ERROR(Parser::get_token(p_stream, token, r_line, r_error));
	if (token.type != Parser::TK_STRING && !(p_constraint && token.type == Parser::TK_IDENTIFIER)) {
		r_error = "Expected class name in ClassRef.";
		return ERR_PARSE_ERROR;
	}
	r_name = token.value;
	RETURN_IF_ERROR(Parser::get_token(p_stream, token, r_line, r_error));
	if (token.type != (p_constraint ? Parser::TK_BRACKET_CLOSE : Parser::TK_PARENTHESIS_CLOSE)) {
		r_error = p_constraint ? "Expected ']' after ClassRef base." : "Expected ')' after ClassRef class name.";
		return ERR_PARSE_ERROR;
	}
	return OK;
}
} // namespace

Error WGodotClassReferenceSerialization::parse(VariantParser::Stream *p_stream, Variant &r_value, int &r_line, String &r_error, bool p_syntax_only) {
	StringName name;
	RETURN_IF_ERROR(read_name(p_stream, name, r_line, r_error, false));
	if (p_syntax_only) {
		r_value = Variant();
		return OK;
	}
	r_value = WGodotClassReferences::resolve(name);
	if (WGodotClassReferences::name(r_value) == StringName()) {
		r_error = "Unknown ClassRef class '" + String(name) + "'.";
		return ERR_PARSE_ERROR;
	}
	return OK;
}

Error WGodotClassReferenceSerialization::parse_type(VariantParser::Stream *p_stream, StringName &r_constraint, int &r_line, String &r_error, bool p_syntax_only) {
	StringName name;
	RETURN_IF_ERROR(read_name(p_stream, name, r_line, r_error, true));
	if (!p_syntax_only && WGodotClassReferences::name(WGodotClassReferences::resolve(name)) == StringName()) {
		r_error = "Unknown ClassRef base '" + String(name) + "'.";
		return ERR_PARSE_ERROR;
	}
	r_constraint = WGodotClassReferences::constraint(name);
	return OK;
}

String WGodotClassReferenceSerialization::type_text(const StringName &p_constraint) {
	const String base = WGodotClassReferences::constraint_base(p_constraint);
	return base.is_empty() ? String(p_constraint) : "ClassRef[\"" + base.c_escape() + "\"]";
}
