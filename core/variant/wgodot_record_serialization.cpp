// wgodot-changes::file
#include "wgodot_record_serialization.h"

namespace {
using Parser = VariantParser;

Error expect(Parser::Stream *p_stream, Parser::TokenType p_type, int &r_line, String &r_error, Parser::Token &r_token) {
	RETURN_IF_ERROR(Parser::get_token(p_stream, r_token, r_line, r_error));
	if (r_token.type != p_type) {
		r_error = "Unexpected token in record/value constructor.";
		return ERR_PARSE_ERROR;
	}
	return OK;
}
} // namespace

Dictionary WGodotRecordSerialization::fields(const WRecord &p_value) {
	Dictionary result;
	if (!p_value.is_empty()) {
		for (int index = 0; index < p_value.schema()->field_count(); ++index) {
			result[p_value.schema()->field(index).name] = p_value.get(index);
		}
	}
	return result;
}

Error WGodotRecordSerialization::restore(const StringName &p_name, const Dictionary &p_fields, Variant &r_value) {
	if (p_name == StringName() && p_fields.is_empty()) {
		r_value = WRecord();
		return OK;
	}
	const Ref<WRecordSchema> schema = WRecordSchemas::resolve(p_name);
	bool valid = false;
	r_value = WRecord::create(schema, p_fields, valid);
	return valid ? OK : ERR_INVALID_DATA;
}

Error WGodotRecordSerialization::parse(const String &p_constructor, Parser::Stream *p_stream, int &r_line, String &r_error, Variant &r_value, Parser::ResourceParser *p_resources) {
	Parser::Token token;
	RETURN_IF_ERROR(expect(p_stream, Parser::TK_PARENTHESIS_OPEN, r_line, r_error, token));
	if (p_constructor == "WValue") {
		RETURN_IF_ERROR(Parser::get_token(p_stream, token, r_line, r_error));
		RETURN_IF_ERROR(Parser::parse_value(token, r_value, p_stream, r_line, r_error, p_resources));
		RETURN_IF_ERROR(expect(p_stream, Parser::TK_PARENTHESIS_CLOSE, r_line, r_error, token));
		if (p_resources && p_resources->syntax_only) {
			return OK;
		}
		if (r_value.get_type() != Variant::ARRAY && r_value.get_type() != Variant::DICTIONARY && !Variant::is_packed_array_type(r_value.get_type())) {
			r_error = "WValue requires a container.";
			return ERR_PARSE_ERROR;
		}
		r_value.wgodot_make_value_container();
		return OK;
	}
	RETURN_IF_ERROR(expect(p_stream, Parser::TK_STRING, r_line, r_error, token));
	const StringName name = token.value;
	RETURN_IF_ERROR(expect(p_stream, Parser::TK_COMMA, r_line, r_error, token));
	RETURN_IF_ERROR(expect(p_stream, Parser::TK_CURLY_BRACKET_OPEN, r_line, r_error, token));
	Variant values;
	RETURN_IF_ERROR(Parser::parse_value(token, values, p_stream, r_line, r_error, p_resources));
	RETURN_IF_ERROR(expect(p_stream, Parser::TK_PARENTHESIS_CLOSE, r_line, r_error, token));
	if (p_resources && p_resources->syntax_only) {
		return OK;
	}
	const Error error = restore(name, values, r_value);
	if (error != OK) {
		r_error = "Invalid record declaration or fields for " + String(name) + ".";
	}
	return error;
}

Error WGodotRecordSerialization::write(const WRecord &p_value, VariantWriter::StoreStringFunc p_store, void *p_store_data, VariantWriter::EncodeResourceFunc p_encode, void *p_encode_data, bool p_pretty, int p_depth, bool p_compat) {
	RETURN_IF_ERROR(p_store(p_store_data, "WRecord("));
	const String name = p_value.is_empty() ? String() : String(p_value.schema()->get_name());
	RETURN_IF_ERROR(VariantWriter::write(name, p_store, p_store_data, p_encode, p_encode_data, p_pretty, p_depth + 1, p_compat));
	RETURN_IF_ERROR(p_store(p_store_data, ", "));
	RETURN_IF_ERROR(VariantWriter::write(fields(p_value), p_store, p_store_data, p_encode, p_encode_data, p_pretty, p_depth + 1, p_compat));
	return p_store(p_store_data, ")");
}
