// wgodot-changes::file

#include "wgodot_gdscript_formatter.h"

#include "wgodot_cpp_ast.h"

#include "core/string/string_builder.h"
#include "core/templates/hash_set.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_utility_functions.h"

namespace WGodotGDScriptFormatter {

namespace {

using Token = GDScriptTokenizer::Token;
using Parser = GDScriptParser;

constexpr int COMMENT = Token::TK_MAX;
constexpr int INFINITE_WIDTH = 100000000;

bool is_open(int p_type) {
	return p_type == Token::PARENTHESIS_OPEN || p_type == Token::BRACKET_OPEN || p_type == Token::BRACE_OPEN;
}

bool is_close(int p_type) {
	return p_type == Token::PARENTHESIS_CLOSE || p_type == Token::BRACKET_CLOSE || p_type == Token::BRACE_CLOSE;
}

struct Lexeme {
	Token token;
	int type = Token::EMPTY;
	int offset = 0;
	int end = 0;
	int matching = -1;
	bool unary = false;
	bool node_path = false;
	String text;
};

struct Source {
	String text;
	PackedStringArray lines;
	Vector<int> offsets;
	Vector<Lexeme> tokens;

	int position(int p_line, int p_column) const {
		if (p_line < 1 || p_line > offsets.size()) {
			return text.length();
		}
		return offsets[p_line - 1] + p_column - 1;
	}

	bool read(const String &p_text, String &r_error) {
		text = p_text;
		lines = text.split("\n");
		int offset = 0;
		for (const String &line : lines) {
			offsets.push_back(offset);
			offset += line.length() + 1;
		}
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(text);
		tokenizer.set_multiline_mode(true);
		while (true) {
			Token token = tokenizer.scan();
			if (token.type == Token::TK_EOF) {
				break;
			}
			if (token.type == Token::ERROR) {
				r_error = vformat("Line %d: %s", token.start_line, String(token.literal));
				return false;
			}
			if (token.type == Token::NEWLINE || token.type == Token::INDENT || token.type == Token::DEDENT) {
				continue;
			}
			Lexeme lexeme;
			lexeme.token = token;
			lexeme.type = token.type;
			lexeme.offset = position(token.start_line, token.start_column);
			lexeme.end = lexeme.offset + token.source.length();
			lexeme.text = token.source;
			tokens.push_back(lexeme);
		}
		for (const KeyValue<int, GDScriptTokenizer::CommentData> &entry : tokenizer.get_comments()) {
			Lexeme comment;
			comment.type = COMMENT;
			comment.token.start_line = entry.key;
			comment.token.end_line = entry.key;
			comment.text = entry.value.comment.trim_suffix("\r");
			const String &line = lines[entry.key - 1];
			comment.offset = offsets[entry.key - 1] + line.length() - entry.value.comment.length();
			comment.end = comment.offset + comment.text.length();
			tokens.push_back(comment);
		}
		tokens.sort_custom<Source>();
		Vector<int> brackets;
		int previous = -1;
		for (int i = 0; i < tokens.size(); i++) {
			Lexeme &token = tokens.write[i];
			if (token.type == COMMENT) {
				continue;
			}
			if (is_open(token.type)) {
				brackets.push_back(i);
			} else if (is_close(token.type) && !brackets.is_empty()) {
				const int opening = brackets[brackets.size() - 1];
				brackets.resize(brackets.size() - 1);
				token.matching = opening;
				tokens.write[opening].matching = i;
			}
			token.unary = token.type == Token::TILDE || token.type == Token::BANG || token.type == Token::NOT ||
					((token.type == Token::PLUS || token.type == Token::MINUS) &&
							(previous < 0 || !tokens[previous].token.can_precede_bin_op()));
			previous = i;
		}
		return true;
	}

	bool operator()(const Lexeme &p_left, const Lexeme &p_right) const {
		return p_left.offset < p_right.offset;
	}
};

// The existing visitor follows owned AST children only. The signature records
// structure as well as operators; source tokens separately protect names and values.
class SyntaxInfo : public WGodotCppAstVisitor {
	const Source &source;

protected:
	bool visit(const Parser::Node *p_node) override {
		signature.push_back(p_node->type);
		switch (p_node->type) {
			case Parser::Node::SUITE: {
				const auto *suite = static_cast<const Parser::SuiteNode *>(p_node);
				signature.push_back(suite->statements.size());
				if (!suite->statements.is_empty()) {
					const auto *first = suite->statements[0];
					suite_starts.insert(source.position(first->start_line, first->start_column));
				}
			} break;
			case Parser::Node::CLASS:
				signature.push_back(static_cast<const Parser::ClassNode *>(p_node)->members.size());
				break;
			case Parser::Node::FUNCTION:
				signature.push_back(static_cast<const Parser::FunctionNode *>(p_node)->parameters.size());
				break;
			case Parser::Node::ARRAY:
				signature.push_back(static_cast<const Parser::ArrayNode *>(p_node)->elements.size());
				break;
			case Parser::Node::DICTIONARY:
				signature.push_back(static_cast<const Parser::DictionaryNode *>(p_node)->elements.size());
				break;
			case Parser::Node::CALL:
				signature.push_back(static_cast<const Parser::CallNode *>(p_node)->arguments.size());
				break;
			case Parser::Node::BINARY_OPERATOR:
				signature.push_back(static_cast<const Parser::BinaryOpNode *>(p_node)->operation);
				break;
			case Parser::Node::UNARY_OPERATOR:
				signature.push_back(static_cast<const Parser::UnaryOpNode *>(p_node)->operation);
				break;
			case Parser::Node::ASSIGNMENT:
				signature.push_back(static_cast<const Parser::AssignmentNode *>(p_node)->operation);
				break;
			case Parser::Node::GET_NODE:
				node_paths.push_back(Vector2i(source.position(p_node->start_line, p_node->start_column), source.position(p_node->end_line, p_node->end_column)));
				break;
			default:
				break;
		}
		return true;
	}

public:
	Vector<int> signature;
	HashSet<int> suite_starts;
	Vector<Vector2i> node_paths;

	explicit SyntaxInfo(const Source &p_source) :
			source(p_source) {}
};

// A small document algebra: groups fit on one line or expand together. Child
// groups can still fit independently, as in Ruff's document printer.
class Documents {
	struct Document {
		enum Kind { TEXT,
			LINE,
			SOFT_LINE,
			HARD_LINE,
			CONCAT,
			INDENT,
			GROUP,
			FLAT,
			IF_BREAK } kind = TEXT;
		String text;
		Vector<int> children;
		int width = 0;
		bool force = false;
	};
	Vector<Document> documents;
	StringBuilder output;
	int column = 0;
	bool line_start = true;
	int line_length;

	void write(const String &p_text, int p_indent) {
		if (p_text.is_empty()) {
			return;
		}
		if (line_start) {
			output.append(String("\t").repeat(p_indent));
			column = p_indent * 4;
			line_start = false;
		}
		output.append(p_text);
		const int newline = p_text.rfind("\n");
		column = newline < 0 ? column + p_text.length() : p_text.length() - newline - 1;
		line_start = p_text.ends_with("\n");
	}

	void render(int p_document, int p_indent, bool p_flat, int p_tail = 0) {
		const Document &doc = documents[p_document];
		switch (doc.kind) {
			case Document::TEXT:
				write(doc.text, p_indent);
				break;
			case Document::IF_BREAK:
				if (!p_flat) {
					write(doc.text, p_indent);
				}
				break;
			case Document::LINE:
			case Document::SOFT_LINE:
			case Document::HARD_LINE:
				if (p_flat && doc.kind != Document::HARD_LINE) {
					if (doc.kind == Document::LINE) {
						write(" ", p_indent);
					}
				} else {
					output.append("\n");
					column = 0;
					line_start = true;
				}
				break;
			case Document::CONCAT: {
				Vector<int> tails;
				tails.resize(doc.children.size());
				int tail = p_tail;
				for (int i = doc.children.size() - 1; i >= 0; i--) {
					tails.write[i] = tail;
					const Document &child = documents[doc.children[i]];
					if (child.kind == Document::TEXT || child.kind == Document::FLAT) {
						tail = MIN(INFINITE_WIDTH, tail + child.width);
					} else if (child.kind == Document::IF_BREAK) {
						tail += p_flat ? 0 : child.text.length();
					} else {
						tail = 0;
					}
				}
				for (int i = 0; i < doc.children.size(); i++) {
					render(doc.children[i], p_indent, p_flat, tails[i]);
				}
			} break;
			case Document::INDENT:
				render(doc.children[0], p_indent + (p_flat ? 0 : 1), p_flat, p_tail);
				break;
			case Document::FLAT:
				render(doc.children[0], p_indent, true, p_tail);
				break;
			case Document::GROUP: {
				const int used = line_start ? p_indent * 4 : column;
				render(doc.children[0], p_indent, !doc.force && (p_flat || used + doc.width + p_tail <= line_length), p_tail);
			} break;
		}
	}

	int append(const Document &p_document) {
		documents.push_back(p_document);
		return documents.size() - 1;
	}

public:
	explicit Documents(int p_line_length) :
			line_length(p_line_length) {}

	int text(const String &p_text) {
		Document doc;
		doc.text = p_text;
		doc.width = p_text.contains("\n") ? INFINITE_WIDTH : p_text.length();
		return append(doc);
	}

	int line(bool p_soft = false, bool p_hard = false) {
		Document doc;
		doc.kind = p_hard ? Document::HARD_LINE : (p_soft ? Document::SOFT_LINE : Document::LINE);
		doc.width = p_hard ? INFINITE_WIDTH : (p_soft ? 0 : 1);
		return append(doc);
	}

	int concat(const Vector<int> &p_children) {
		Document doc;
		doc.kind = Document::CONCAT;
		doc.children = p_children;
		for (int child : p_children) {
			doc.width = MIN(INFINITE_WIDTH, doc.width + documents[child].width);
		}
		return append(doc);
	}

	int indent(int p_child) {
		Document doc;
		doc.kind = Document::INDENT;
		doc.children.push_back(p_child);
		doc.width = documents[p_child].width;
		return append(doc);
	}

	int group(int p_child, bool p_force = false) {
		Document doc;
		doc.kind = Document::GROUP;
		doc.children.push_back(p_child);
		doc.width = p_force ? INFINITE_WIDTH : documents[p_child].width;
		doc.force = p_force;
		return append(doc);
	}

	int if_break(const String &p_text) {
		Document doc;
		doc.kind = Document::IF_BREAK;
		doc.text = p_text;
		return append(doc);
	}

	int flat(int p_child) {
		Document doc;
		doc.kind = Document::FLAT;
		doc.children.push_back(p_child);
		doc.width = documents[p_child].width;
		return append(doc);
	}

	String print(int p_document, int p_indent) {
		render(p_document, p_indent, false);
		return output.as_string();
	}
};

String normalize_string(const Lexeme &p_token) {
	const String &text = p_token.text;
	if (p_token.type != Token::LITERAL || text.contains("\n")) {
		return text;
	}
	int quote = 0;
	if (text.begins_with("&") || text.begins_with("^") || text.begins_with("r")) {
		quote = 1;
	}
	if (quote >= text.length() || text[quote] != '\'' || text.substr(quote).begins_with("'''")) {
		return text;
	}
	// Keep the original quote if changing it would add escapes. Raw strings
	// keep all backslashes; ordinary strings may drop an escaped apostrophe.
	const String body = text.substr(quote + 1, text.length() - quote - 2);
	if (body.contains("\"")) {
		return text;
	}
	StringBuilder result;
	result.append(text.left(quote));
	result.append("\"");
	for (int i = 0; i < body.length(); i++) {
		if (body[i] == '\\' && i + 1 < body.length()) {
			if (body[i + 1] == '\'' && !text.begins_with("r")) {
				i++;
			} else {
				result.append(String::chr(body[i++]));
			}
		}
		result.append(String::chr(body[i]));
	}
	result.append("\"");
	return result.as_string();
}

int operator_priority(const Lexeme &p_token) {
	if (p_token.unary || p_token.node_path) {
		return 0;
	}
	switch (p_token.type) {
		case Token::IF:
		case Token::ELSE:
			return 1;
		case Token::OR:
		case Token::PIPE_PIPE:
			return 2;
		case Token::AND:
		case Token::AMPERSAND_AMPERSAND:
			return 3;
		case Token::LESS:
		case Token::LESS_EQUAL:
		case Token::GREATER:
		case Token::GREATER_EQUAL:
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::TK_IN:
		case Token::IS:
			return 4;
		case Token::PIPE:
		case Token::CARET:
		case Token::AMPERSAND:
			return 5;
		case Token::LESS_LESS:
		case Token::GREATER_GREATER:
			return 6;
		case Token::PLUS:
		case Token::MINUS:
			return 7;
		case Token::STAR:
		case Token::SLASH:
		case Token::PERCENT:
		case Token::STAR_STAR:
			return 8;
		default:
			return 0;
	}
}

class Layout {
	const Source &source;
	const SyntaxInfo &syntax;
	Documents docs;

	// Read a primary and its call/attribute/index trailers. Only dots following
	// a call start new segments, so qualified names and subscripts stay together.
	int find_call_chain(int p_begin, int p_end, Vector<int> &r_segments) const {
		const Lexeme &first = source.tokens[p_begin];
		int next = p_begin + 1;
		if (first.node_path) {
			while (next < p_end && source.tokens[next].node_path) {
				next++;
			}
		} else if (is_open(first.type) && first.matching > p_begin && first.matching < p_end) {
			next = first.matching + 1;
		} else if (!first.token.is_identifier() && first.type != Token::SELF && first.type != Token::SUPER && first.type != Token::LITERAL && first.type != Token::PRELOAD && first.type != Token::ASYNC_PRELOAD) {
			return p_begin;
		}
		int calls = 0;
		bool after_call = false;
		while (next < p_end) {
			const Lexeme &token = source.tokens[next];
			if ((token.type == Token::PARENTHESIS_OPEN || token.type == Token::BRACKET_OPEN) && token.matching > next && token.matching < p_end) {
				if (token.type == Token::PARENTHESIS_OPEN) {
					calls++;
					after_call = true;
				}
				next = token.matching + 1;
			} else if (token.type == Token::PERIOD && next + 1 < p_end && source.tokens[next + 1].token.is_identifier()) {
				if (after_call) {
					r_segments.push_back(next);
				}
				after_call = false;
				next += 2;
			} else {
				break;
			}
		}
		if (calls < 2 || r_segments.is_empty()) {
			r_segments.clear();
			return p_begin;
		}
		return next;
	}

	int call_chain(int p_begin, int p_end, const Vector<int> &p_segments) {
		Vector<int> parts;
		int start = p_begin;
		for (int segment : p_segments) {
			parts.push_back(sequence(start, segment));
			parts.push_back(docs.line(true));
			start = segment;
		}
		parts.push_back(sequence(start, p_end));
		return docs.group(docs.concat(parts));
	}

	bool space_before(int p_index, int p_previous) const {
		if (p_previous < 0) {
			return false;
		}
		const Lexeme &current = source.tokens[p_index];
		const Lexeme &previous = source.tokens[p_previous];
		const int type = current.type;
		const int before = previous.type;
		if (current.node_path && previous.node_path) {
			return false;
		}
		if (type == Token::COLON && p_index + 1 < source.tokens.size() && source.tokens[p_index + 1].type == Token::EQUAL) {
			return true;
		}
		if (before == Token::COLON && type == Token::EQUAL) {
			return false;
		}
		if (type == Token::COMMA || type == Token::COLON || type == Token::SEMICOLON || type == Token::PERIOD || is_close(type)) {
			return false;
		}
		if (is_open(before) || before == Token::PERIOD || before == Token::DOLLAR) {
			return false;
		}
		if (previous.unary && before != Token::NOT) {
			// Keep a separately tokenized numeric operand separate from its sign.
			return (before == Token::MINUS || before == Token::PLUS) && type == Token::LITERAL;
		}
		if (before == Token::PERCENT && previous.node_path) {
			return false;
		}
		if (type == Token::PARENTHESIS_OPEN) {
			return !(previous.token.can_precede_bin_op() || previous.token.is_identifier() || before == Token::ANNOTATION || before == Token::FUNC || before == Token::ASSERT || before == Token::PRELOAD || before == Token::ASYNC_PRELOAD || before == Token::SUPER);
		}
		if (type == Token::BRACKET_OPEN) {
			return !previous.token.can_precede_bin_op();
		}
		return true;
	}

	int sequence(int p_begin, int p_end, bool p_break_operators = false) {
		Vector<int> parts;
		int priority = 0;
		if (p_break_operators) {
			for (int i = p_begin; i < p_end; i++) {
				const Lexeme &token = source.tokens[i];
				if (is_open(token.type) && token.matching >= i && token.matching < p_end) {
					i = token.matching;
					continue;
				}
				const int value = operator_priority(token);
				if (value > 0 && (priority == 0 || value < priority)) {
					priority = value;
				}
			}
		}
		int previous = -1;
		bool after_comment = false;
		for (int i = p_begin; i < p_end; i++) {
			const Lexeme &token = source.tokens[i];
			if (token.type == COMMENT) {
				const bool inline_comment = previous >= 0 && source.tokens[previous].token.end_line == token.token.start_line;
				if (inline_comment) {
					parts.push_back(docs.text("  "));
				} else if (!parts.is_empty() && !after_comment) {
					parts.push_back(docs.line(false, true));
				}
				parts.push_back(docs.text(token.text));
				if (i + 1 < p_end) {
					parts.push_back(docs.line(false, true));
				}
				after_comment = true;
				previous = -1;
				continue;
			}
			if (previous >= 0 && priority > 0 && operator_priority(token) == priority) {
				parts.push_back(docs.line());
			} else if (space_before(i, previous)) {
				parts.push_back(docs.text(" "));
			}
			Vector<int> chain_segments;
			const int chain_end = p_break_operators ? find_call_chain(i, p_end, chain_segments) : i;
			if (chain_end > i) {
				parts.push_back(call_chain(i, chain_end, chain_segments));
				i = chain_end - 1;
			} else if (is_open(token.type) && token.matching >= i && token.matching < p_end) {
				parts.push_back(delimited(i, token.matching, previous));
				i = token.matching;
			} else {
				parts.push_back(docs.text(normalize_string(token)));
			}
			previous = i;
			after_comment = false;
		}
		return docs.concat(parts);
	}

	int delimited(int p_open, int p_close, int p_previous) {
		const Lexeme &opening = source.tokens[p_open];
		Vector<int> commas;
		bool has_comment = false;
		bool has_lambda = false;
		for (int i = p_open + 1; i < p_close; i++) {
			const Lexeme &token = source.tokens[i];
			has_comment |= token.type == COMMENT;
			has_lambda |= token.type == Token::FUNC;
			if (is_open(token.type) && token.matching > i) {
				i = token.matching;
			} else if (token.type == Token::COMMA) {
				commas.push_back(i);
			}
		}
		if (p_open + 1 == p_close) {
			return docs.text(opening.text + source.tokens[p_close].text);
		}
		const bool subscript = opening.type == Token::BRACKET_OPEN && p_previous >= 0 && source.tokens[p_previous].token.can_precede_bin_op();
		if (subscript) {
			bool type_arguments = true;
			for (int i = p_open + 1; i < p_close; i++) {
				const int type = source.tokens[i].type;
				if (type != Token::IDENTIFIER && type != Token::PERIOD && type != Token::COMMA && type != Token::BRACKET_OPEN && type != Token::BRACKET_CLOSE) {
					type_arguments = false;
					break;
				}
			}
			if (type_arguments) {
				Vector<int> parts;
				parts.push_back(docs.text(opening.text));
				parts.push_back(sequence(p_open + 1, p_close));
				parts.push_back(docs.text(source.tokens[p_close].text));
				return docs.flat(docs.concat(parts));
			}
		}
		const bool call = opening.type == Token::PARENTHESIS_OPEN && p_previous >= 0 &&
				(source.tokens[p_previous].token.can_precede_bin_op() || source.tokens[p_previous].token.is_identifier() || source.tokens[p_previous].type == Token::ANNOTATION ||
						source.tokens[p_previous].type == Token::FUNC || source.tokens[p_previous].type == Token::ASSERT ||
						source.tokens[p_previous].type == Token::PRELOAD || source.tokens[p_previous].type == Token::ASYNC_PRELOAD || source.tokens[p_previous].type == Token::SUPER);
		const bool comma_allowed = !subscript && !has_lambda && (call || opening.type == Token::BRACKET_OPEN || opening.type == Token::BRACE_OPEN);
		int last_code = p_close - 1;
		while (last_code > p_open && source.tokens[last_code].type == COMMENT) {
			last_code--;
		}
		const bool trailing_comma = source.tokens[last_code].type == Token::COMMA;
		Vector<int> body;
		body.push_back(docs.line(true));
		int start = p_open + 1;
		for (int comma : commas) {
			body.push_back(docs.group(sequence(start, comma, !has_lambda)));
			body.push_back(docs.text(","));
			start = comma + 1;
			if (start < p_close && source.tokens[start].type == COMMENT && source.tokens[start].token.start_line == source.tokens[comma].token.end_line) {
				body.push_back(docs.text("  " + source.tokens[start].text));
				start++;
			}
			if (start < p_close) {
				body.push_back(docs.line(false, has_comment));
			}
		}
		if (start < p_close) {
			// A trailing comment belongs after the comma we introduce when expanded.
			int comment = p_close;
			if (source.tokens[p_close - 1].type == COMMENT && p_close - 1 > start && source.tokens[p_close - 2].token.end_line == source.tokens[p_close - 1].token.start_line) {
				comment--;
			}
			body.push_back(docs.group(sequence(start, comment, !has_lambda)));
			if (comma_allowed && !trailing_comma && last_code >= start) {
				body.push_back(docs.if_break(","));
			}
			if (comment < p_close) {
				body.push_back(docs.text("  " + source.tokens[comment].text));
			}
		}
		Vector<int> group;
		group.push_back(docs.text(opening.text));
		group.push_back(docs.indent(docs.concat(body)));
		group.push_back(docs.line(true));
		group.push_back(docs.text(source.tokens[p_close].text));
		return docs.group(docs.concat(group), has_comment || trailing_comma);
	}

	int expression(int p_begin, int p_end) {
		bool has_breaks = false;
		for (int i = p_begin; i < p_end; i++) {
			const Lexeme &token = source.tokens[i];
			if (token.type == Token::FUNC) {
				return sequence(p_begin, p_end);
			}
			Vector<int> chain_segments;
			const int chain_end = find_call_chain(i, p_end, chain_segments);
			if (chain_end > i) {
				has_breaks = true;
				i = chain_end - 1;
			} else if (is_open(token.type) && token.matching > i && token.matching < p_end) {
				i = token.matching;
			} else if (operator_priority(token) > 0) {
				has_breaks = true;
			}
		}
		if (!has_breaks) {
			return sequence(p_begin, p_end);
		}
		Vector<int> parts;
		parts.push_back(docs.if_break("("));
		Vector<int> body;
		body.push_back(docs.line(true));
		body.push_back(docs.group(sequence(p_begin, p_end, true)));
		parts.push_back(docs.indent(docs.concat(body)));
		parts.push_back(docs.line(true));
		parts.push_back(docs.if_break(")"));
		return docs.group(docs.concat(parts));
	}

	int statement(int p_begin, int p_end) {
		if (p_begin >= p_end) {
			return docs.text("");
		}
		Vector<int> parts;
		// Expand single-line suites, but never the body of an inline lambda.
		for (int i = p_begin; i < p_end; i++) {
			const Lexeme &token = source.tokens[i];
			if (is_open(token.type) && token.matching > i && token.matching < p_end) {
				i = token.matching;
				continue;
			}
			if (token.type == Token::FUNC && i != p_begin && source.tokens[i - 1].type != Token::STATIC) {
				break;
			}
			if (token.type == Token::COLON && i + 1 < p_end && syntax.suite_starts.has(source.tokens[i + 1].offset)) {
				parts.push_back(statement(p_begin, i + 1));
				Vector<int> body;
				body.push_back(docs.line(false, true));
				body.push_back(statement(i + 1, p_end));
				parts.push_back(docs.indent(docs.concat(body)));
				return docs.concat(parts);
			}
			if (token.type == Token::SEMICOLON) {
				parts.push_back(statement(p_begin, i));
				if (i + 1 < p_end) {
					parts.push_back(docs.line(false, true));
					parts.push_back(statement(i + 1, p_end));
				}
				return docs.concat(parts);
			}
		}
		int end = p_end;
		if (source.tokens[end - 1].type == COMMENT) {
			end--;
		}
		int expr = -1;
		const int first = source.tokens[p_begin].type;
		if (first == Token::RETURN || first == Token::IF || first == Token::ELIF || first == Token::WHILE || first == Token::MATCH) {
			expr = p_begin + 1;
		}
		for (int i = p_begin; i < end; i++) {
			const Lexeme &token = source.tokens[i];
			if (is_open(token.type) && token.matching > i && token.matching < end) {
				i = token.matching;
				continue;
			}
			if (token.type == Token::FUNC) {
				expr = -1;
				break;
			}
			if (token.type >= Token::EQUAL && token.type <= Token::CARET_EQUAL) {
				expr = i + 1;
				break;
			}
			if (first == Token::FOR && token.type == Token::TK_IN) {
				expr = i + 1;
				break;
			}
		}
		int expr_end = end;
		if (expr_end > p_begin && source.tokens[expr_end - 1].type == Token::COLON) {
			expr_end--;
		}
		if (expr >= 0 && expr < expr_end) {
			parts.push_back(sequence(p_begin, expr));
			parts.push_back(docs.text(" "));
			parts.push_back(expression(expr, expr_end));
			parts.push_back(sequence(expr_end, end));
		} else {
			Vector<int> chain_segments;
			if (find_call_chain(p_begin, end, chain_segments) == end) {
				parts.push_back(expression(p_begin, end));
			} else {
				parts.push_back(sequence(p_begin, end));
			}
		}
		if (end < p_end) {
			parts.push_back(docs.text((end > p_begin ? "  " : "") + source.tokens[end].text));
		}
		return docs.group(docs.concat(parts));
	}

public:
	Layout(const Source &p_source, const SyntaxInfo &p_syntax, int p_width) :
			source(p_source), syntax(p_syntax), docs(p_width) {}

	String format(int p_begin, int p_end, int p_indent) {
		return docs.print(statement(p_begin, p_end), p_indent);
	}

	String format_line(int p_begin, int p_end, int p_indent) {
		return docs.print(docs.group(sequence(p_begin, p_end)), p_indent);
	}
};

struct Unit {
	int begin = 0;
	int end = 0;
	int first_line = 0;
	int last_line = 0;
	int indent = 0;
	int blanks = 0;
	bool verbatim = false;
	bool multiline_lambda = false;
	bool declaration = false;
	bool attachment = false;
};

int indentation(const String &p_line) {
	int width = 0;
	for (int i = 0; i < p_line.length(); i++) {
		if (p_line[i] == '\t') {
			width += 4;
		} else if (p_line[i] == ' ') {
			width++;
		} else {
			break;
		}
	}
	return width;
}

Vector<Unit> collect_units(const Source &p_source) {
	Vector<Unit> units;
	int begin = 0;
	bool disabled = false;
	while (begin < p_source.tokens.size()) {
		Unit unit;
		unit.begin = begin;
		unit.first_line = p_source.tokens[begin].token.start_line;
		unit.verbatim = disabled;
		int depth = 0;
		bool lambda = false;
		int end = begin;
		for (; end < p_source.tokens.size(); end++) {
			const Lexeme &token = p_source.tokens[end];
			if (is_open(token.type)) {
				depth++;
			} else if (is_close(token.type)) {
				depth--;
			}
			if (token.type == Token::FUNC && (depth > 0 || (end > begin && p_source.tokens[end - 1].type != Token::STATIC))) {
				lambda = true;
			}
			if (token.type == COMMENT) {
				const String directive = token.text.trim_prefix("#").strip_edges();
				if (directive == "fmt: off") {
					disabled = true;
					unit.verbatim = true;
				} else if (directive == "fmt: on") {
					disabled = false;
					unit.verbatim = true;
				} else if (directive == "fmt: skip") {
					unit.verbatim = true;
				}
			}
			unit.last_line = token.token.end_line;
			if (depth == 0 && (end + 1 == p_source.tokens.size() || p_source.tokens[end + 1].token.start_line > token.token.end_line)) {
				const String &line = p_source.lines[token.token.end_line - 1];
				if (!line.strip_edges().ends_with("\\")) {
					end++;
					break;
				}
			}
		}
		unit.end = end;
		unit.multiline_lambda = lambda && unit.last_line > unit.first_line;
		unit.indent = indentation(p_source.lines[unit.first_line - 1]);
		const int first = p_source.tokens[begin].type;
		unit.declaration = first == Token::FUNC || first == Token::CLASS || (first == Token::STATIC && begin + 1 < end && p_source.tokens[begin + 1].type == Token::FUNC);
		unit.attachment = first == COMMENT;
		if (first == Token::ANNOTATION) {
			int next = begin + 1;
			if (next < end && p_source.tokens[next].type == Token::PARENTHESIS_OPEN) {
				next = p_source.tokens[next].matching + 1;
			}
			unit.attachment = next == end || (next + 1 == end && p_source.tokens[next].type == COMMENT);
		}
		unit.blanks = units.is_empty() ? 0 : MIN(unit.indent == 0 ? 2 : 1, unit.first_line - units[units.size() - 1].last_line - 1);
		units.push_back(unit);
		begin = end;
	}
	return units;
}

String format_source(Source &p_source, const SyntaxInfo &p_syntax, int p_width) {
	for (const Vector2i &range : p_syntax.node_paths) {
		for (int i = 0; i < p_source.tokens.size(); i++) {
			if (p_source.tokens[i].offset >= range.x && p_source.tokens[i].offset < range.y) {
				p_source.tokens.write[i].node_path = true;
			}
		}
	}
	Vector<Unit> units = collect_units(p_source);
	int indent_size = 0;
	for (const Unit &unit : units) {
		if (unit.indent > 0 && p_source.tokens[unit.begin].type != COMMENT) {
			indent_size = unit.indent;
			break;
		}
	}
	if (indent_size == 0) {
		indent_size = 4;
	}
	for (int i = 0; i < units.size(); i++) {
		if (!units[i].declaration || units[i].verbatim) {
			continue;
		}
		int attached = i;
		while (attached > 0 && units[attached - 1].attachment && units[attached - 1].indent == units[i].indent && !units[attached - 1].verbatim && units[attached].blanks == 0) {
			attached--;
		}
		if (attached > 0) {
			units.write[attached].blanks = units[i].indent == 0 ? 2 : 1;
		}
	}
	StringBuilder result;
	for (int i = 0; i < units.size(); i++) {
		const Unit &unit = units[i];
		int blanks = unit.blanks;
		if (unit.verbatim && i > 0) {
			blanks = unit.first_line - units[i - 1].last_line - 1;
		}
		result.append(String("\n").repeat(blanks));
		if (unit.verbatim) {
			const int start = p_source.offsets[unit.first_line - 1];
			const int end = unit.last_line < p_source.offsets.size() ? p_source.offsets[unit.last_line] : p_source.text.length();
			result.append(p_source.text.substr(start, end - start).trim_suffix("\n"));
		} else if (unit.multiline_lambda) {
			// Lambda suites carry significant newlines inside expression delimiters.
			// Keep those boundaries while formatting each physical statement.
			int start = unit.begin;
			while (start < unit.end) {
				int end = start + 1;
				while (end < unit.end && p_source.tokens[end].token.start_line <= p_source.tokens[end - 1].token.end_line) {
					end++;
				}
				const int line = p_source.tokens[start].token.start_line;
				if (start > unit.begin) {
					result.append(String("\n").repeat(MIN(2, line - p_source.tokens[start - 1].token.end_line)));
				}
				Layout layout(p_source, p_syntax, p_width);
				result.append(layout.format_line(start, end, indentation(p_source.lines[line - 1]) / indent_size));
				start = end;
			}
		} else {
			Layout layout(p_source, p_syntax, p_width);
			result.append(layout.format(unit.begin, unit.end, unit.indent / indent_size));
		}
		result.append("\n");
	}
	return result.as_string();
}

bool parse(GDScriptParser &r_parser, const String &p_source, const String &p_path, String &r_error) {
	if (r_parser.parse(p_source, p_path, false) == OK) {
		return true;
	}
	if (!r_parser.get_errors().is_empty()) {
		const auto &error = r_parser.get_errors().front()->get();
		r_error = vformat("Line %d: %s", error.start_line, error.message);
	} else {
		r_error = "Could not parse GDScript.";
	}
	return false;
}

Vector<int> significant_tokens(const Source &p_source) {
	Vector<int> result;
	for (int i = 0; i < p_source.tokens.size(); i++) {
		const int type = p_source.tokens[i].type;
		if (type == Token::PARENTHESIS_OPEN || type == Token::PARENTHESIS_CLOSE || type == Token::SEMICOLON) {
			continue;
		}
		if (type == Token::COMMA) {
			int next = i + 1;
			while (next < p_source.tokens.size() && p_source.tokens[next].type == COMMENT) {
				next++;
			}
			if (next < p_source.tokens.size() && is_close(p_source.tokens[next].type)) {
				continue;
			}
		}
		result.push_back(i);
	}
	return result;
}

// Only resolved instance accesses are eligible. Declaration names and attributes
// on another object are not references to the current instance.
class ExplicitSelf : public WGodotCppAstVisitor {
	const Source &source;
	const Parser::FunctionNode *function = nullptr;

	void qualify(const Parser::IdentifierNode *p_identifier) {
		offsets.insert(source.position(p_identifier->start_line, p_identifier->start_column));
	}

	bool is_accessor_storage(const Parser::IdentifierNode *p_identifier) const {
		if (!function || p_identifier->source != Parser::IdentifierNode::MEMBER_VARIABLE || !p_identifier->variable_source) {
			return false;
		}
		const auto *variable = p_identifier->variable_source;
		if (variable->property == Parser::VariableNode::PROP_INLINE) {
			return variable->getter == function || variable->setter == function;
		}
		if (variable->property == Parser::VariableNode::PROP_SETGET && function->identifier) {
			const StringName name = function->identifier->name;
			return (variable->getter_pointer && variable->getter_pointer->name == name) ||
					(variable->setter_pointer && variable->setter_pointer->name == name);
		}
		return false;
	}

protected:
	bool visit(const Parser::Node *p_node) override {
		switch (p_node->type) {
			case Parser::Node::FUNCTION: {
				const auto *previous = function;
				function = static_cast<const Parser::FunctionNode *>(p_node);
				bool valid = true;
				for (const auto *parameter : function->parameters) {
					valid = walk(parameter) && valid;
				}
				valid = walk(function->rest_parameter) && walk(function->body) && valid;
				function = previous;
				return valid;
			}
			case Parser::Node::CALL: {
				const auto *call = static_cast<const Parser::CallNode *>(p_node);
				if (call->callee && call->callee->type == Parser::Node::IDENTIFIER) {
					const auto *identifier = static_cast<const Parser::IdentifierNode *>(call->callee);
					// Built-in constructors and utilities bypass instance method resolution.
					if (!call->is_super && !call->is_static &&
							Parser::get_builtin_type(identifier->name) == Variant::VARIANT_MAX &&
							!GDScriptUtilityFunctions::function_exists(identifier->name) &&
							!Variant::has_utility_function(identifier->name)) {
						qualify(identifier);
					}
				} else if (!walk(call->callee)) {
					return false;
				}
				for (const auto *argument : call->arguments) {
					if (!walk(argument)) {
						return false;
					}
				}
				return true;
			}
			case Parser::Node::SUBSCRIPT: {
				const auto *subscript = static_cast<const Parser::SubscriptNode *>(p_node);
				return walk(subscript->base) && (subscript->is_attribute || walk(subscript->index));
			}
			case Parser::Node::IDENTIFIER: {
				const auto *identifier = static_cast<const Parser::IdentifierNode *>(p_node);
				const bool member = identifier->source == Parser::IdentifierNode::MEMBER_VARIABLE ||
						identifier->source == Parser::IdentifierNode::INHERITED_VARIABLE ||
						identifier->source == Parser::IdentifierNode::MEMBER_SIGNAL ||
						(identifier->source == Parser::IdentifierNode::MEMBER_FUNCTION && !identifier->function_source_is_static);
				if (member && !is_accessor_storage(identifier)) {
					qualify(identifier);
				}
			} break;
			default:
				break;
		}
		return true;
	}

	bool descend(const Parser::Node *p_node) override {
		return p_node->type != Parser::Node::FUNCTION && p_node->type != Parser::Node::CALL && p_node->type != Parser::Node::SUBSCRIPT;
	}

public:
	HashSet<int> offsets;
	explicit ExplicitSelf(const Source &p_source) :
			source(p_source) {}
};

bool analyze(Parser &r_parser, String &r_error) {
	GDScriptAnalyzer analyzer(&r_parser);
	if (analyzer.analyze() == OK) {
		return true;
	}
	r_error = "Cannot resolve instance members for explicit_self; file was not changed.";
	if (!r_parser.get_errors().is_empty()) {
		const auto &error = r_parser.get_errors().front()->get();
		r_error += vformat(" Line %d: %s", error.start_line, error.message);
	}
	r_error += " Set explicit_self=false in wgformat.cfg to format without member qualification.";
	return false;
}

bool qualify_members(const String &p_source, const String &p_path, String &r_output, String &r_error) {
	Parser parser;
	if (!parse(parser, p_source, p_path, r_error) || !analyze(parser, r_error)) {
		return false;
	}
	Source source;
	if (!source.read(p_source, r_error)) {
		return false;
	}
	ExplicitSelf members(source);
	if (!members.walk(parser.get_tree())) {
		r_error = "Unsupported GDScript syntax tree.";
		return false;
	}
	StringBuilder output;
	int copied = 0;
	for (const Unit &unit : collect_units(source)) {
		if (unit.verbatim) {
			continue;
		}
		for (int i = unit.begin; i < unit.end; i++) {
			const Lexeme &token = source.tokens[i];
			if (token.type == Token::IDENTIFIER && members.offsets.has(token.offset)) {
				output.append(p_source.substr(copied, token.offset - copied));
				output.append("self.");
				copied = token.offset;
			}
		}
	}
	output.append(p_source.substr(copied));
	r_output = output.as_string();
	if (r_output != p_source) {
		Parser qualified;
		if (!parse(qualified, r_output, p_path, r_error) || !analyze(qualified, r_error)) {
			r_error = "Explicit-self qualification failed validation. " + r_error;
			return false;
		}
	}
	return true;
}

bool same_tokens(const Source &p_before, const Source &p_after) {
	const Vector<int> before = significant_tokens(p_before);
	const Vector<int> after = significant_tokens(p_after);
	if (before.size() != after.size()) {
		return false;
	}
	for (int i = 0; i < before.size(); i++) {
		const Lexeme &left = p_before.tokens[before[i]];
		const Lexeme &right = p_after.tokens[after[i]];
		if (left.type != right.type) {
			return false;
		}
		if (left.type == Token::LITERAL && (left.token.literal.get_type() == Variant::STRING || left.token.literal.get_type() == Variant::STRING_NAME || left.token.literal.get_type() == Variant::NODE_PATH)) {
			if (left.token.literal.get_type() != right.token.literal.get_type() || left.token.literal != right.token.literal) {
				return false;
			}
		} else if (left.text != right.text) {
			return false;
		}
	}
	return true;
}

bool format_layout(const String &p_source, const String &p_path, const Options &p_options, String &r_output, String &r_error) {
	Parser parser;
	if (!parse(parser, p_source, p_path, r_error)) {
		return false;
	}
	Source original;
	if (!original.read(p_source, r_error)) {
		return false;
	}
	SyntaxInfo original_syntax(original);
	if (!original_syntax.walk(parser.get_tree())) {
		r_error = "Unsupported GDScript syntax tree.";
		return false;
	}
	r_output = format_source(original, original_syntax, p_options.line_length);
	if (r_output == p_source) {
		return true;
	}
	Parser formatted_parser;
	if (!parse(formatted_parser, r_output, p_path, r_error)) {
		r_error = "Formatter produced invalid syntax; file was not changed. " + r_error;
		if (!formatted_parser.get_errors().is_empty()) {
			const int line = formatted_parser.get_errors().front()->get().start_line;
			const PackedStringArray lines = r_output.split("\n");
			for (int i = MAX(0, line - 3); i < MIN(lines.size(), line + 1); i++) {
				r_error += vformat("\n  %d | %s", i + 1, lines[i]);
			}
		}
		return false;
	}
	Source formatted;
	if (!formatted.read(r_output, r_error)) {
		return false;
	}
	SyntaxInfo formatted_syntax(formatted);
	if (!formatted_syntax.walk(formatted_parser.get_tree()) || original_syntax.signature != formatted_syntax.signature || !same_tokens(original, formatted)) {
		r_error = "Formatter changed syntax structure or token values; file was not changed.";
		return false;
	}
	return true;
}

} // namespace

bool format(const String &p_source, const String &p_path, const Options &p_options, String &r_output, String &r_error) {
	String source = p_source;
	if (p_options.explicit_self && !qualify_members(p_source, p_path, source, r_error)) {
		return false;
	}
	return format_layout(source, p_path, p_options, r_output, r_error);
}

} // namespace WGodotGDScriptFormatter
