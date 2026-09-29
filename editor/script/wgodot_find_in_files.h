// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"
#include "core/templates/local_vector.h"

// Tracks lexical state across lines without changing search text or columns.
class WGodotSearchComments {
	enum class Language {
		NONE,
		GDSCRIPT,
		SHADER,
	};
	struct Span {
		int begin;
		int end;
	};
	Language language = Language::NONE;
	char32_t quote = 0;
	bool triple_quote = false;
	bool block_comment = false;
	LocalVector<Span> comments;
	uint32_t next_comment = 0;

public:
	explicit WGodotSearchComments(const String &p_extension);
	void scan_line(const String &p_line);
	// Search results arrive in increasing column order within each line.
	bool overlaps(int p_begin, int p_end);
};
