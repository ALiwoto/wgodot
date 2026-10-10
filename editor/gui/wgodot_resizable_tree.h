// wgodot-changes::file
#pragma once

#include "scene/gui/tree.h"

// Opt-in editor table with draggable column-header borders.
class WGodotResizableTree : public Tree {
	int resizing_column = -1;
	real_t drag_origin = 0;
	int original_width = 0;

	int _column_border_at(const Point2 &p_position) const;

public:
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	virtual CursorShape get_cursor_shape(const Point2 &p_position = Point2()) const override;
};
