// wgodot-changes::file
#include "wgodot_find_in_files.h"

#include "find_in_files.h"

#include "core/object/callable_mp.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/flow_container.h"

WGodotSearchComments::WGodotSearchComments(const String &p_extension) {
	if (p_extension == "gd") {
		language = Language::GDSCRIPT;
	} else if (p_extension == "gdshader" || p_extension == "gdshaderinc") {
		language = Language::SHADER;
	}
}

void WGodotSearchComments::scan_line(const String &p_line) {
	comments.clear();
	next_comment = 0;
	if (language == Language::NONE) {
		return;
	}
	const char32_t *text = p_line.ptr();
	const int length = p_line.length();
	int comment_begin = 0;
	int column = 0;
	while (column < length) {
		if (block_comment) {
			const int end = p_line.find("*/", column);
			if (end < 0) {
				comments.push_back({ comment_begin, length });
				return;
			}
			column = end + 2;
			comments.push_back({ comment_begin, column });
			block_comment = false;
			continue;
		}
		if (quote) {
			if (text[column] == '\\') {
				column += 2;
			} else if (text[column] == quote && (!triple_quote || (column + 2 < length && text[column + 1] == quote && text[column + 2] == quote))) {
				column += triple_quote ? 3 : 1;
				quote = 0;
			} else {
				column++;
			}
			continue;
		}
		if (language == Language::GDSCRIPT && text[column] == '#') {
			comments.push_back({ column, length });
			return;
		}
		if (language == Language::SHADER && text[column] == '/' && column + 1 < length) {
			if (text[column + 1] == '/') {
				comments.push_back({ column, length });
				return;
			}
			if (text[column + 1] == '*') {
				comment_begin = column;
				block_comment = true;
				column += 2;
				// Include an opening delimiter even when it ends the line.
				if (column == length) {
					comments.push_back({ comment_begin, length });
				}
				continue;
			}
		}
		if (text[column] == '"' || (language == Language::GDSCRIPT && text[column] == '\'')) {
			quote = text[column];
			triple_quote = language == Language::GDSCRIPT && column + 2 < length && text[column + 1] == quote && text[column + 2] == quote;
			column += triple_quote ? 3 : 1;
			continue;
		}
		column++;
	}
}

bool WGodotSearchComments::overlaps(int p_begin, int p_end) {
	while (next_comment < comments.size() && comments[next_comment].end <= p_begin) {
		next_comment++;
	}
	return next_comment < comments.size() && comments[next_comment].begin < p_end;
}

void FindInFilesSearchPanel::_wgodot_add_search_filters() {
	HFlowContainer *row = memnew(HFlowContainer);
	additional_options_vbc->add_child(row);
	filters_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->add_child(filters_container);

	comments_checkbox = memnew(CheckBox);
	comments_checkbox->set_text(TTRC("Comments"));
	comments_checkbox->set_tooltip_text(TTRC("Include comments in GDScript and shader search results."));
	comments_checkbox->set_pressed_no_signal(true);
	comments_checkbox->connect(SceneStringName(toggled), callable_mp(this, &FindInFilesSearchPanel::_on_search_modified).unbind(1));
	row->add_child(comments_checkbox);
}
