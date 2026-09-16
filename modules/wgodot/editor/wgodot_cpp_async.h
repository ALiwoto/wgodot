// wgodot-changes::file
#pragma once

#include "wgodot_cpp_emitter.h"

// Lowers only functions which can suspend. The generated native frame owns
// saved values; resume labels leave ordinary synchronous blocks intact.
class WGodotCppAsync {
	using Parser = GDScriptParser;
	WGodotCppEmitter &emitter;
	HashMap<const Parser::ExpressionNode *, bool> await_nodes;
	HashMap<String, String> field_types;
	Vector<String> temporary_fields;
	String fields;
	String code;
	int resume_count = 0;
	int field_count = 0;

	Vector<const Parser::ExpressionNode *> children(const Parser::ExpressionNode *p_expression) const;
	bool has_await(const Parser::ExpressionNode *p_expression);
	String add_field(const String &p_type, const String &p_name);
	String local(const Parser::Node *p_node, const StringName &p_name, const Parser::DataType &p_type);
	void line(int p_indent, const String &p_code);
	void clear_temporaries(int p_indent);
	bool borrows_slot(const Parser::ExpressionNode *p_expression) const;
	String preserve(const Parser::ExpressionNode *p_expression, int p_indent);
	String expression(const Parser::ExpressionNode *p_expression, int p_indent);
	String condition(const Parser::ExpressionNode *p_expression, int p_indent);
	void suite(const Parser::SuiteNode *p_suite, int p_indent, bool p_clear_locals = true);

public:
	explicit WGodotCppAsync(WGodotCppEmitter &p_emitter) : emitter(p_emitter) {}
	String generate(const Parser::FunctionNode *p_function, const String &p_name, const Vector<String> &p_parameters, String &r_declaration);
};
