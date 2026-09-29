// wgodot-changes::file
#include "wgodot_native_string_format.h"

#include "wgodot_native_wdictionary.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

#include <cstring>

namespace WGodotNative {
namespace {

class NamedFormatOutput {
	struct Replacement {
		int start;
		int length;
		String value;
	};
	LocalVector<Replacement> replacements;

public:
	void add(int p_start, int p_length, const String &p_value);
	String finish(const String &p_source) const;
};

// Arbitrary placeholder patterns use a reversed Aho-Corasick matcher. Scanning
// backwards finds the longest token at each source position without enumerating
// every overlapping match. Replacement text never enters the matcher.
class NamedFormatMatcher {
	struct Node {
		HashMap<char32_t, int> edges;
		int failure = 0;
		int match = -1;
	};
	struct Token {
		int length;
		String value;
	};
	LocalVector<Node> nodes;
	LocalVector<Token> tokens;

public:
	NamedFormatMatcher();
	void add(const String &p_token, const String &p_value);
	String format(const String &p_source);
};

void NamedFormatOutput::add(int p_start, int p_length, const String &p_value) {
	replacements.push_back({ p_start, p_length, p_value });
}

String NamedFormatOutput::finish(const String &p_source) const {
	if (replacements.is_empty()) {
		return p_source;
	}
	int64_t length = p_source.length();
	for (const Replacement &replacement : replacements) {
		length += int64_t(replacement.value.length()) - replacement.length;
	}
	ERR_FAIL_COND_V_MSG(length >= INT32_MAX, String(), "Formatted string is too long.");
	if (length == 0) {
		return String();
	}
	String result;
	ERR_FAIL_COND_V(result.resize_uninitialized(length + 1) != OK, String());
	char32_t *destination = result.ptrw();
	int source_offset = 0;
	for (const Replacement &replacement : replacements) {
		const int literal_length = replacement.start - source_offset;
		memcpy(destination, p_source.ptr() + source_offset, literal_length * sizeof(char32_t));
		destination += literal_length;
		if (!replacement.value.is_empty()) {
			memcpy(destination, replacement.value.ptr(), replacement.value.length() * sizeof(char32_t));
			destination += replacement.value.length();
		}
		source_offset = replacement.start + replacement.length;
	}
	const int tail_length = p_source.length() - source_offset;
	memcpy(destination, p_source.ptr() + source_offset, tail_length * sizeof(char32_t));
	destination[tail_length] = 0;
	return result;
}

NamedFormatMatcher::NamedFormatMatcher() {
	nodes.push_back(Node());
}

void NamedFormatMatcher::add(const String &p_token, const String &p_value) {
	if (p_token.is_empty()) {
		return;
	}
	int node = 0;
	for (int i = p_token.length() - 1; i >= 0; i--) {
		const int *child = nodes[node].edges.getptr(p_token[i]);
		if (child) {
			node = *child;
		} else {
			const int next = nodes.size();
			nodes[node].edges.insert(p_token[i], next);
			nodes.push_back(Node());
			node = next;
		}
	}
	// Equal tokens keep the first dictionary entry; other overlaps prefer the
	// earliest source position and then the longest token at that position.
	if (nodes[node].match < 0) {
		nodes[node].match = tokens.size();
		tokens.push_back({ p_token.length(), p_value });
	}
}

String NamedFormatMatcher::format(const String &p_source) {
	LocalVector<int> queue;
	for (const auto &edge : nodes[0].edges) {
		queue.push_back(edge.value);
	}
	for (uint32_t i = 0; i < queue.size(); i++) {
		const int node = queue[i];
		const int inherited = nodes[nodes[node].failure].match;
		if (nodes[node].match < 0) {
			nodes[node].match = inherited;
		}
		for (const auto &edge : nodes[node].edges) {
			int failure = nodes[node].failure;
			const int *next = nodes[failure].edges.getptr(edge.key);
			while (failure && !next) {
				failure = nodes[failure].failure;
				next = nodes[failure].edges.getptr(edge.key);
			}
			nodes[edge.value].failure = next ? *next : 0;
			queue.push_back(edge.value);
		}
	}
	LocalVector<int> matches;
	matches.resize_uninitialized(p_source.length());
	int node = 0;
	for (int i = p_source.length() - 1; i >= 0; i--) {
		const int *next = nodes[node].edges.getptr(p_source[i]);
		while (node && !next) {
			node = nodes[node].failure;
			next = nodes[node].edges.getptr(p_source[i]);
		}
		node = next ? *next : 0;
		matches[i] = nodes[node].match;
	}
	NamedFormatOutput output;
	for (int i = 0; i < p_source.length(); i++) {
		if (matches[i] >= 0) {
			const Token &token = tokens[matches[i]];
			output.add(i, token.length, token.value);
			i += token.length - 1;
		}
	}
	return output.finish(p_source);
}

inline bool named_format_matches(const String &p_source, int p_offset, const String &p_token) {
	return p_token.length() <= p_source.length() - p_offset &&
			memcmp(p_source.ptr() + p_offset, p_token.ptr(), p_token.length() * sizeof(char32_t)) == 0;
}

} // namespace

String format_named(const String &p_source, const WDictionary<String, String> &p_values, const String &p_placeholder) {
	if (p_source.is_empty() || p_values.is_empty() || p_placeholder.is_empty()) {
		return p_source;
	}
	const int marker = p_placeholder.find_char('_');
	const String prefix = p_placeholder.substr(0, marker);
	const String suffix = marker < 0 ? String() : p_placeholder.substr(marker + 1);
	bool delimited = marker > 0 && !suffix.is_empty() && suffix.find_char('_') < 0 && prefix[0] != suffix[0];
	if (delimited) {
		// A key containing a delimiter can overlap other tokens; let the general
		// matcher resolve those cases with the same leftmost-longest rule.
		for (const auto &entry : p_values.native()) {
			const String &key = entry.key;
			if (key.find_char(prefix[0]) >= 0 || key.find_char(suffix[0]) >= 0) {
				delimited = false;
				break;
			}
		}
	}
	if (delimited) {
		NamedFormatOutput output;
		int start = -1;
		int key_start = 0;
		for (int i = 0; i < p_source.length(); i++) {
			if (p_source[i] == prefix[0] && named_format_matches(p_source, i, prefix)) {
				start = i;
				key_start = i + prefix.length();
			} else if (start >= 0 && i >= key_start && p_source[i] == suffix[0] && named_format_matches(p_source, i, suffix)) {
				const auto *value = p_values.getptr(p_source.substr(key_start, i - key_start));
				if (value) {
					output.add(start, i + suffix.length() - start, *value);
					i += suffix.length() - 1;
				}
				start = -1;
			}
		}
		return output.finish(p_source);
	}
	NamedFormatMatcher matcher;
	for (const auto &entry : p_values.native()) {
		matcher.add(p_placeholder.replace("_", entry.key), entry.value);
	}
	return matcher.format(p_source);
}

} // namespace WGodotNative
