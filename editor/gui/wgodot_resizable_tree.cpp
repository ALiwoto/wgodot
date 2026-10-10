// wgodot-changes::file
#include "wgodot_resizable_tree.h"

#include "editor/themes/editor_scale.h"
#include "scene/resources/style_box.h"

int WGodotResizableTree::_column_border_at(const Point2 &p_position) const {
	if (!are_column_titles_visible() || !get_root()) {
		return -1;
	}
	const real_t top = get_theme_stylebox(SNAME("panel"), SNAME("Tree"))->get_margin(SIDE_TOP);
	// The root's unscrolled offset is exactly the header height, including for
	// an empty table. Cell rectangles supply the actual RTL/scroll-aware edges.
	if (p_position.y < top || p_position.y >= top + get_item_offset(get_root())) {
		return -1;
	}
	for (int column = 0; column < get_columns(); column++) {
		const Rect2 cell = get_item_rect(get_root(), column);
		const real_t edge = is_layout_rtl() ? cell.position.x : cell.get_end().x;
		if (Math::abs(p_position.x - edge) <= 5 * EDSCALE) {
			return column;
		}
	}
	return -1;
}

void WGodotResizableTree::gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (button->is_pressed()) {
			resizing_column = _column_border_at(button->get_position());
			if (resizing_column >= 0) {
				// Preserve displayed widths before disabling automatic expansion.
				// This lets a drag shrink the first column and keeps neighbors still.
				Vector<int> widths;
				for (int column = 0; column < get_columns(); column++) {
					widths.push_back(get_column_width(column));
				}
				for (int column = 0; column < get_columns(); column++) {
					set_column_clip_content(column, true);
					set_column_expand(column, false);
					set_column_custom_minimum_width(column, widths[column]);
				}
				drag_origin = button->get_position().x;
				original_width = widths[resizing_column];
				accept_event();
				return;
			}
		} else if (resizing_column >= 0) {
			resizing_column = -1;
			accept_event();
			return; // Releasing a resize must not sort the column.
		}
	}
	const Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && resizing_column >= 0) {
		if (motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
			const real_t distance = (motion->get_position().x - drag_origin) * (is_layout_rtl() ? -1 : 1);
			set_column_custom_minimum_width(resizing_column, MAX(int(32 * EDSCALE), int(original_width + distance)));
			accept_event();
			return;
		}
		resizing_column = -1;
	}
	Tree::gui_input(p_event);
}

Control::CursorShape WGodotResizableTree::get_cursor_shape(const Point2 &p_position) const {
	if (resizing_column >= 0 || _column_border_at(p_position) >= 0) {
		return CURSOR_HSPLIT;
	}
	return Tree::get_cursor_shape(p_position);
}
