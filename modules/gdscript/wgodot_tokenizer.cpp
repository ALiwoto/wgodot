// wgodot-changes::file

#include "gdscript_tokenizer.h"

GDScriptTokenizer::Token GDScriptTokenizerText::wgodot_escaped_identifier() {
	_advance(); // Consume first "{".
	_advance(); // Consume second "{".

	while (!_is_at_end()) {
		if (_peek() == '\n' || _peek() == '\r') {
			return make_error(R"(Unterminated wgodot escaped identifier.)");
		}
		if (_peek() == '}' && _peek(1) == '}') {
			_advance();
			_advance();
			Token identifier = make_token(Token::IDENTIFIER);
			identifier.literal = StringName(identifier.source);
			return identifier;
		}
		_advance();
	}

	return make_error(R"(Unterminated wgodot escaped identifier.)");
}
