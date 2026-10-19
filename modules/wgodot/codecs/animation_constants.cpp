// wgodot-changes::file
#include "animation_constants.h"
#include "animation_numbers.h"

#include "core/string/string_builder.h"

namespace {
bool digit(char32_t p_char) {
	return p_char >= '0' && p_char <= '9';
}

bool word(char32_t p_char) {
	return digit(p_char) || (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z') || p_char == '_';
}

// Used only while saving. Skip quoted resource paths and constructor names;
// this scanner is never involved in animation evaluation or key editing.
template <typename F>
void numbers(const String &p_text, F p_visit) {
	int position = 0;
	while (position < p_text.length()) {
		char32_t character = p_text[position];
		if (character == '"') {
			position++;
			while (position < p_text.length()) {
				character = p_text[position++];
				if (character == '\\') {
					position++;
				} else if (character == '"') {
					break;
				}
			}
		} else if (digit(character) || character == '-' || character == '+' || character == '.') {
			const int begin = position++;
			while (position < p_text.length()) {
				character = p_text[position];
				if (!digit(character) && character != '.' && character != '-' && character != '+' && character != 'e' && character != 'E') {
					break;
				}
				position++;
			}
			p_visit(begin, position - begin);
		} else if (word(character)) {
			while (position < p_text.length() && word(p_text[position])) {
				position++;
			}
		} else {
			position++;
		}
	}
}
} // namespace

String WAnimationConstants::name(uint32_t p_id) {
	static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	char buffer[8];
	int begin = sizeof(buffer);
	do {
		buffer[--begin] = digits[p_id % 36];
		p_id /= 36;
	} while (p_id);
	return "c_" + String::utf8(buffer + begin, sizeof(buffer) - begin);
}

bool WAnimationConstants::parse_id(const String &p_name, uint32_t &r_id) {
	if (p_name.length() < 3 || p_name[0] != 'c' || p_name[1] != '_' || p_name[2] == '0') {
		return false;
	}
	uint64_t value = 0;
	for (int i = 2; i < p_name.length(); i++) {
		char32_t character = p_name[i];
		int part = character >= '0' && character <= '9' ? character - '0' : character >= 'a' && character <= 'z' ? character - 'a' + 10 : -1;
		if (part < 0) {
			return false;
		}
		value = value * 36 + part;
		if (value >= INT32_MAX) {
			return false;
		}
	}
	r_id = value;
	return true;
}

void WAnimationConstants::count(const String &p_expression) {
	numbers(p_expression, [&](int p_begin, int p_length) {
		String literal = p_expression.substr(p_begin, p_length);
		Candidate *candidate = candidates.getptr(literal);
		if (!candidate) {
			Candidate value;
			value.literal = literal;
			value.order = candidates.size();
			candidate = &candidates.insert(literal, value)->value;
		}
		candidate->count++;
	});
}

void WAnimationConstants::select(const Vector<WAnimation::CodecConstant> &p_previous) {
	HashMap<String, uint32_t> previous;
	for (int i = 1; i < p_previous.size(); i++) {
		if (p_previous[i].defined) {
			previous.insert(w_animation_number(p_previous[i].value), i);
		}
	}
	uint32_t next_id = MAX(1, p_previous.size());
	LocalVector<Candidate *> ordered;
	for (KeyValue<String, Candidate> &entry : candidates) {
		ordered.push_back(&entry.value);
	}
	ordered.sort_custom<FrequentFirst>();
	for (Candidate *candidate : ordered) {
		if (candidate->count < 2) {
			continue;
		}
		const uint32_t *existing = previous.getptr(candidate->literal);
		const uint32_t id = existing ? *existing : next_id;
		const int reference_length = name(id).length();
		const int saved_per_use = candidate->literal.length() - reference_length;
		const int declaration_bytes = reference_length + candidate->literal.length() + 5;
		if (saved_per_use <= 0 || candidate->count * saved_per_use <= uint64_t(declaration_bytes)) {
			continue;
		}
		candidate->id = id;
		selected.push_back(candidate);
		if (!existing) {
			next_id++;
		}
	}
	selected.sort_custom<ByID>();
}

String WAnimationConstants::encode(const String &p_expression) const {
	StringBuilder result;
	int copied = 0;
	numbers(p_expression, [&](int p_begin, int p_length) {
		const Candidate *candidate = candidates.getptr(p_expression.substr(p_begin, p_length));
		if (candidate && candidate->id) {
			result += p_expression.substr(copied, p_begin - copied);
			result += name(candidate->id);
			copied = p_begin + p_length;
		}
	});
	result += p_expression.substr(copied);
	return result.as_string();
}

String WAnimationConstants::declarations() const {
	StringBuilder result;
	result += "{";
	for (uint32_t i = 0; i < selected.size(); i++) {
		if (i) {
			result += ", ";
		}
		result += name(selected[i]->id) + " = " + selected[i]->literal;
	}
	result += "}";
	return result.as_string();
}

Vector<WAnimation::CodecConstant> WAnimationConstants::save_hints() const {
	Vector<WAnimation::CodecConstant> result;
	if (!selected.is_empty()) {
		result.resize(selected[selected.size() - 1]->id + 1);
		for (const Candidate *candidate : selected) {
			result.write[candidate->id] = { candidate->literal.to_float(), true };
		}
	}
	return result;
}
