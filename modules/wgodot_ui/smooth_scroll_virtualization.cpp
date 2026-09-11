// wgodot-changes::file
#include "smooth_scroll_element.h"

#include "core/input/input.h"
#include "scene/main/viewport.h"
#include "servers/display/accessibility_server.h"

struct SmoothScrollElement::VirtualGrid {
	struct Item {
		ObjectID view;
		int index = -1;
		Node::ProcessMode process_mode = Node::PROCESS_MODE_INHERIT;
	};
	ObjectID content;
	Callable create;
	Callable bind;
	Callable unbind;
	Callable accessibility_info;
	RID accessibility_content;
	Vector<RID> accessibility_items;
	bool rtl = false;
	Vector<Item> pool;
	int count = 0;
	int columns = 1;
	int buffer_rows = 2;
	int captured_index = -1;
	Vector2 item_size = Vector2(1, 1);
	Vector2 separation;
	Vector4 margins;
	bool refresh = false;

	Rect2 item_rect(int p_index) const {
		const int column = rtl ? columns - 1 - p_index % columns : p_index % columns;
		return Rect2(Vector2(margins.x, margins.y) + Vector2(column, p_index / columns) * (item_size + separation), item_size);
	}
	void clear_accessibility() {
		if (accessibility_content.is_valid()) {
			AccessibilityServer::get_singleton()->free_element(accessibility_content);
			accessibility_content = RID();
		}
		accessibility_items.clear();
	}
	Control *get_content() const {
		return Object::cast_to<Control>(ObjectDB::get_instance(content));
	}
};

void SmoothScrollElement::set_virtual_content(Control *p_content, const Callable &p_create, const Callable &p_bind, const Callable &p_unbind) {
	ERR_FAIL_NULL(p_content);
	ERR_FAIL_COND_MSG(p_content->get_parent() != this, "Virtual content must be a direct child of this SmoothScrollElement.");
	ERR_FAIL_COND_MSG(!p_create.is_valid() || !p_bind.is_valid() || !p_unbind.is_valid(), "Virtual content requires create, bind and unbind callbacks.");
	clear_virtual_content();
	virtual_grid = memnew(VirtualGrid);
	virtual_grid->content = p_content->get_instance_id();
	virtual_grid->create = p_create;
	virtual_grid->bind = p_bind;
	virtual_grid->unbind = p_unbind;
	set_notify_transform(true);
	connect_virtual_focus();
	queue_virtual_update();
}

void SmoothScrollElement::clear_virtual_content() {
	if (!virtual_grid) {
		return;
	}
	VirtualGrid *grid = virtual_grid;
	if (is_inside_tree()) {
		get_viewport()->disconnect(SNAME("gui_focus_changed"), callable_mp(this, &SmoothScrollElement::virtual_focus_changed));
	}
	virtual_grid = nullptr;
	virtual_focus_inside = false;
	grid->clear_accessibility();
	queue_accessibility_update();
	for (const VirtualGrid::Item &item : grid->pool) {
		Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
		if (view) {
			if (item.index >= 0 && grid->unbind.is_valid()) {
				grid->unbind.call(view, item.index);
			}
			memdelete(view);
		}
	}
	memdelete(grid);
}

SmoothScrollElement::~SmoothScrollElement() {
	// Views belong to the content node and are freed by normal Node ownership.
	// Script callbacks are not invoked during object destruction.
	if (virtual_grid) {
		memdelete(virtual_grid);
	}
}

void SmoothScrollElement::set_virtual_grid(int p_count, int p_columns, const Vector2 &p_item_size, const Vector2 &p_separation, const Vector4 &p_margins) {
	ERR_FAIL_NULL_MSG(virtual_grid, "Set virtual content before configuring its grid.");
	ERR_FAIL_COND(p_count < 0 || p_columns < 1);
	ERR_FAIL_COND(!p_item_size.is_finite() || p_item_size.x <= 0 || p_item_size.y <= 0);
	ERR_FAIL_COND(!p_separation.is_finite() || p_separation.x < 0 || p_separation.y < 0);
	ERR_FAIL_COND(!p_margins.is_finite() || p_margins.x < 0 || p_margins.y < 0 || p_margins.z < 0 || p_margins.w < 0);
	VirtualGrid &grid = *virtual_grid;
	if (grid.count != p_count) {
		grid.clear_accessibility();
	}
	grid.count = p_count;
	grid.rtl = is_layout_rtl();
	grid.columns = p_columns;
	grid.item_size = p_item_size;
	grid.separation = p_separation;
	grid.margins = p_margins;
	Control *content = grid.get_content();
	ERR_FAIL_NULL(content);
	const int rows = p_count / p_columns + (p_count % p_columns != 0);
	const Vector2 extent(p_margins.x + p_margins.z + p_columns * p_item_size.x + (p_columns - 1) * p_separation.x,
			p_margins.y + p_margins.w + rows * p_item_size.y + MAX(0, rows - 1) * p_separation.y);
	content->set_custom_minimum_size(extent);
	// ScrollContainer caches its largest child extent during this calculation.
	// Refresh it before the queued layout updates the scrollbar ranges.
	(void)get_minimum_size();
	queue_sort();
	queue_virtual_update();
}

void SmoothScrollElement::set_virtual_buffer_rows(int p_rows) {
	ERR_FAIL_COND(p_rows < 0);
	ERR_FAIL_NULL(virtual_grid);
	virtual_grid->buffer_rows = p_rows;
	queue_virtual_update();
}

int SmoothScrollElement::get_virtual_buffer_rows() const {
	return virtual_grid ? virtual_grid->buffer_rows : 2;
}

void SmoothScrollElement::queue_virtual_update() {
	if (virtual_grid && is_inside_tree() && !virtual_update_pending) {
		virtual_update_pending = true;
		callable_mp(this, &SmoothScrollElement::update_virtual_grid).call_deferred();
	}
}

void SmoothScrollElement::update_virtual_grid() {
	virtual_update_pending = false;
	if (!virtual_grid || !is_inside_tree()) {
		return;
	}
	VirtualGrid &grid = *virtual_grid;
	Control *content = grid.get_content();
	if (!content || !content->is_inside_tree()) {
		return;
	}

	// Use actual canvas transforms, including elastic displacement and ancestor
	// clipping. ScrollBar values alone are clamped during an overdrag.
	Rect2 visible_rect = get_global_transform_with_canvas().xform(Rect2(Vector2(), get_size()));
	visible_rect = visible_rect.intersection(get_viewport_rect());
	for (CanvasItem *ancestor = get_parent_item(); ancestor; ancestor = ancestor->get_parent_item()) {
		Control *control = Object::cast_to<Control>(ancestor);
		if (control && control->is_clipping_contents()) {
			visible_rect = visible_rect.intersection(control->get_global_transform_with_canvas().xform(Rect2(Vector2(), control->get_size())));
		}
	}
	int first = 0;
	int end = 0;
	if (content->is_visible_in_tree() && visible_rect.has_area()) {
		visible_rect = content->get_global_transform_with_canvas().affine_inverse().xform(visible_rect);
		const double stride = grid.item_size.y + grid.separation.y;
		const double right_edge = grid.margins.x + grid.columns * (grid.item_size.x + grid.separation.x) - grid.separation.x;
		const int first_row = MAX(0, int(Math::floor((visible_rect.position.y - grid.margins.y) / stride)) - grid.buffer_rows);
		const int end_row = MAX(0, int(Math::ceil((visible_rect.get_end().y - grid.margins.y) / stride)) + grid.buffer_rows);
		first = MIN(grid.count, first_row * grid.columns);
		end = MIN(grid.count, end_row * grid.columns);
		// A screen transition can leave only the content's empty side margin
		// intersecting the viewport. No item in that strip needs a view.
		if (visible_rect.get_end().x <= grid.margins.x || visible_rect.position.x >= right_edge) {
			first = end = 0;
		}
	}

	Control *focus = get_viewport()->gui_get_focus_owner();
	HashMap<int, int> active;
	Vector<int> available;
	const bool refresh = grid.refresh;
	grid.refresh = false;
	for (int slot = 0; slot < grid.pool.size(); slot++) {
		VirtualGrid::Item &item = grid.pool.write[slot];
		Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
		if (!view) {
			item.index = -1;
			available.push_back(slot);
			continue;
		}
		const bool pinned = item.index >= 0 && item.index < grid.count &&
				(item.index == grid.captured_index || (focus && (view == focus || view->is_ancestor_of(focus))));
		if (item.index >= 0 && (item.index >= grid.count || (!pinned && (item.index < first || item.index >= end)))) {
			grid.unbind.call(view, item.index);
			item.index = -1;
			view->hide();
			view->set_process_mode(Node::PROCESS_MODE_DISABLED);
		}
		if (item.index >= 0) {
			active.insert(item.index, slot);
			view->set_position(grid.item_rect(item.index).position);
			view->set_size(grid.item_size);
			if (refresh) {
				grid.bind.call(view, item.index);
			}
		} else {
			available.push_back(slot);
		}
	}

	for (int index = first; index < end; index++) {
		if (active.has(index)) {
			continue;
		}
		int slot;
		if (available.is_empty()) {
			slot = grid.pool.size();
			grid.pool.push_back(VirtualGrid::Item());
		} else {
			slot = available[available.size() - 1];
			available.resize(available.size() - 1);
		}
		VirtualGrid::Item &item = grid.pool.write[slot];
		Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
		if (!view) {
			Variant created = grid.create.call(grid.item_size);
			view = Object::cast_to<Control>(created);
			ERR_FAIL_NULL_MSG(view, "Virtual item factory must return a Control.");
			ERR_FAIL_COND_MSG(view->get_parent(), "Virtual item factory must return an unparented Control.");
			item.view = view->get_instance_id();
			item.process_mode = view->get_process_mode();
			view->connect(SNAME("gui_input"), callable_mp(this, &SmoothScrollElement::virtual_item_gui_input).bind(view));
			content->add_child(view);
		}
		item.index = index;
		view->set_position(grid.item_rect(index).position);
		view->set_size(grid.item_size);
		view->set_process_mode(item.process_mode);
		grid.bind.call(view, index);
		view->show();
	}

	// Retain at most a viewport-sized pool, even after the data set shrinks.
	const int capacity = MIN(grid.count, (int(Math::ceil(get_size().y / (grid.item_size.y + grid.separation.y))) + 2 * grid.buffer_rows + 2) * grid.columns);
	for (int slot = grid.pool.size() - 1; slot >= 0 && grid.pool.size() > capacity; slot--) {
		const VirtualGrid::Item &item = grid.pool[slot];
		if (item.index < 0) {
			Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
			if (view) {
				memdelete(view);
			}
			grid.pool.remove_at(slot);
		}
	}
	queue_accessibility_update();
}

void SmoothScrollElement::refresh_virtual_items() {
	if (virtual_grid) {
		// A replacement data set must not inherit an old item's pending click.
		if (Control *pressed = get_virtual_item(virtual_grid->captured_index)) {
			pressed->propagate_notification(NOTIFICATION_SCROLL_BEGIN);
		}
		virtual_grid->captured_index = -1;
		virtual_grid->refresh = true;
		queue_virtual_update();
	}
}

void SmoothScrollElement::refresh_virtual_item(int p_index) {
	if (Control *view = get_virtual_item(p_index)) {
		virtual_grid->bind.call(view, p_index);
	}
	if (virtual_grid) {
		queue_accessibility_update();
	}
}

Control *SmoothScrollElement::get_virtual_item(int p_index) const {
	if (virtual_grid) {
		for (const VirtualGrid::Item &item : virtual_grid->pool) {
			if (item.index == p_index && p_index >= 0) {
				return Object::cast_to<Control>(ObjectDB::get_instance(item.view));
			}
		}
	}
	return nullptr;
}

int SmoothScrollElement::get_virtual_item_index(const Control *p_item) const {
	if (virtual_grid && p_item) {
		for (const VirtualGrid::Item &item : virtual_grid->pool) {
			if (item.view == p_item->get_instance_id()) {
				return item.index;
			}
		}
	}
	return -1;
}

TypedArray<Control> SmoothScrollElement::get_virtual_items() const {
	TypedArray<Control> result;
	if (virtual_grid) {
		for (const VirtualGrid::Item &item : virtual_grid->pool) {
			if (item.index >= 0) {
				Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
				if (view) {
					result.push_back(view);
				}
			}
		}
	}
	return result;
}

int SmoothScrollElement::get_virtual_item_count() const {
	return virtual_grid ? virtual_grid->count : 0;
}

int SmoothScrollElement::get_virtual_pool_size() const {
	return virtual_grid ? virtual_grid->pool.size() : 0;
}

void SmoothScrollElement::scroll_to_virtual_item(int p_index) {
	ERR_FAIL_NULL(virtual_grid);
	ERR_FAIL_INDEX(p_index, virtual_grid->count);
	(void)get_minimum_size();
	notification(Container::NOTIFICATION_SORT_CHILDREN);
	const Rect2 rect = virtual_grid->item_rect(p_index);
	Vector2 position = scroll_position;
	const Vector2 page(get_h_scroll_bar()->get_page(), get_v_scroll_bar()->get_page());
	for (int axis = 0; axis < 2; axis++) {
		if (rect.position[axis] < position[axis]) {
			position[axis] = rect.position[axis];
		} else if (rect.get_end()[axis] > position[axis] + page[axis]) {
			position[axis] = rect.get_end()[axis] - page[axis];
		}
	}
	set_scroll_position(position);
	// Layout normally happens deferred. Focus needs its destination this turn.
	notification(Container::NOTIFICATION_SORT_CHILDREN);
	update_virtual_grid();
}

void SmoothScrollElement::focus_virtual_item(int p_index) {
	virtual_focus_changing = true;
	scroll_to_virtual_item(p_index);
	if (Control *view = get_virtual_item(p_index)) {
		view->grab_focus();
	}
	virtual_focus_changing = false;
}

void SmoothScrollElement::virtual_item_gui_input(const Ref<InputEvent> &p_event, Control *p_view) {
	if (!virtual_grid) {
		return;
	}
	Ref<InputEventMouseButton> mouse = p_event;
	Ref<InputEventScreenTouch> touch = p_event;
	if ((mouse.is_valid() && mouse->get_button_index() == MouseButton::LEFT) || touch.is_valid()) {
		virtual_grid->captured_index = p_event->is_pressed() ? get_virtual_item_index(p_view) : -1;
		queue_virtual_update();
	}
	// Keyboard GUI events go directly to the focused Control, not its parents.
	if (Ref<InputEventKey>(p_event).is_valid() || Ref<InputEventJoypadButton>(p_event).is_valid() || Ref<InputEventJoypadMotion>(p_event).is_valid()) {
		virtual_gui_input(p_event);
	}
}

bool SmoothScrollElement::virtual_gui_input(const Ref<InputEvent> &p_event) {
	if (!virtual_grid) {
		return false;
	}
	VirtualGrid &grid = *virtual_grid;
	Ref<InputEventMouseButton> mouse = p_event;
	Ref<InputEventScreenTouch> touch = p_event;
	if ((mouse.is_valid() && mouse->get_button_index() == MouseButton::LEFT) || touch.is_valid()) {
		grid.captured_index = -1;
		if (p_event->is_pressed()) {
			Control *content = grid.get_content();
			if (content) {
				const Vector2 local = mouse.is_valid() ? mouse->get_position() : touch->get_position();
				const Vector2 point = content->get_global_transform().affine_inverse().xform(get_global_transform().xform(local));
				for (const VirtualGrid::Item &item : grid.pool) {
					if (item.index >= 0 && grid.item_rect(item.index).has_point(point)) {
						grid.captured_index = item.index;
						break;
					}
				}
			}
		}
		queue_virtual_update();
	}
	if (!p_event->is_pressed() || (Ref<InputEventKey>(p_event).is_null() && Ref<InputEventJoypadButton>(p_event).is_null() && Ref<InputEventJoypadMotion>(p_event).is_null())) {
		return false;
	}
	Control *focus = get_viewport()->gui_get_focus_owner();
	int index = -1;
	for (const VirtualGrid::Item &item : grid.pool) {
		Control *view = Object::cast_to<Control>(ObjectDB::get_instance(item.view));
		if (item.index >= 0 && view && focus && (view == focus || view->is_ancestor_of(focus))) {
			index = item.index;
			break;
		}
	}
	if (index < 0) {
		return false;
	}
	int next = index;
	if (p_event->is_action_pressed(SNAME("ui_down"), true)) {
		next += grid.columns;
	} else if (p_event->is_action_pressed(SNAME("ui_up"), true)) {
		next -= grid.columns;
	} else if (p_event->is_action_pressed(SNAME("ui_right"), true)) {
		next += is_layout_rtl() ? -1 : 1;
	} else if (p_event->is_action_pressed(SNAME("ui_left"), true)) {
		next += is_layout_rtl() ? 1 : -1;
	} else if (p_event->is_action_pressed(SNAME("ui_focus_next"), true, true)) {
		next++;
		if (next == grid.count) {
			return focus_outside_virtual_content(true);
		}
	} else if (p_event->is_action_pressed(SNAME("ui_focus_prev"), true, true)) {
		next--;
		if (next < 0) {
			return focus_outside_virtual_content(false);
		}
	} else if (p_event->is_action_pressed(SNAME("ui_home"), true)) {
		next = 0;
	} else if (p_event->is_action_pressed(SNAME("ui_end"), true)) {
		next = grid.count - 1;
	} else if (p_event->is_action_pressed(SNAME("ui_page_down"), true)) {
		next += MAX(1, int(get_size().y / (grid.item_size.y + grid.separation.y))) * grid.columns;
	} else if (p_event->is_action_pressed(SNAME("ui_page_up"), true)) {
		next -= MAX(1, int(get_size().y / (grid.item_size.y + grid.separation.y))) * grid.columns;
	} else {
		return false;
	}
	focus_virtual_item(CLAMP(next, 0, grid.count - 1));
	accept_event();
	return true;
}

bool SmoothScrollElement::focus_outside_virtual_content(bool p_forward) {
	Control *content = virtual_grid->get_content();
	Control *candidate = get_viewport()->gui_get_focus_owner();
	HashSet<ObjectID> visited;
	while (candidate && !visited.has(candidate->get_instance_id())) {
		visited.insert(candidate->get_instance_id());
		candidate = p_forward ? candidate->find_next_valid_focus() : candidate->find_prev_valid_focus();
		if (candidate && candidate != content && !content->is_ancestor_of(candidate)) {
			candidate->grab_focus();
			accept_event();
			return true;
		}
	}
	// When the grid is the only focusable region, wrap in logical order.
	focus_virtual_item(p_forward ? 0 : virtual_grid->count - 1);
	accept_event();
	return true;
}

void SmoothScrollElement::connect_virtual_focus() {
	if (is_inside_tree() && !get_viewport()->is_connected(SNAME("gui_focus_changed"), callable_mp(this, &SmoothScrollElement::virtual_focus_changed))) {
		get_viewport()->connect(SNAME("gui_focus_changed"), callable_mp(this, &SmoothScrollElement::virtual_focus_changed));
	}
}

void SmoothScrollElement::virtual_focus_changed(Control *p_focus) {
	if (!virtual_grid) {
		return;
	}
	Control *content = virtual_grid->get_content();
	const bool inside = content && p_focus && content->is_ancestor_of(p_focus);
	const bool entering = inside && !virtual_focus_inside;
	virtual_focus_inside = inside;
	if (entering && !virtual_focus_changing && virtual_grid->count > 0) {
		Input *input = Input::get_singleton();
		if (input->is_action_pressed(SNAME("ui_focus_prev"))) {
			callable_mp(this, &SmoothScrollElement::focus_virtual_item).call_deferred(virtual_grid->count - 1);
		} else if (input->is_action_pressed(SNAME("ui_focus_next"))) {
			callable_mp(this, &SmoothScrollElement::focus_virtual_item).call_deferred(0);
		}
	}
	queue_virtual_update();
}

void SmoothScrollElement::virtual_notification(int p_what) {
	if (!virtual_grid) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_ACCESSIBILITY_UPDATE:
			update_virtual_accessibility();
			break;
		case NOTIFICATION_ACCESSIBILITY_INVALIDATE:
			// Node invalidation already frees all accessibility sub-elements.
			virtual_grid->accessibility_content = RID();
			virtual_grid->accessibility_items.clear();
			break;
		case NOTIFICATION_ENTER_TREE:
			connect_virtual_focus();
			queue_virtual_update();
			break;
		case NOTIFICATION_EXIT_TREE:
			get_viewport()->disconnect(SNAME("gui_focus_changed"), callable_mp(this, &SmoothScrollElement::virtual_focus_changed));
			virtual_focus_inside = false;
			virtual_grid->captured_index = -1;
			break;
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED:
			virtual_grid->rtl = is_layout_rtl();
			queue_virtual_update();
			break;
		case NOTIFICATION_SORT_CHILDREN:
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_RESIZED:
			queue_virtual_update();
			break;
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT:
			virtual_grid->captured_index = -1;
			queue_virtual_update();
			break;
	}
}

void SmoothScrollElement::bind_virtual_methods() {
	ClassDB::bind_method(D_METHOD("set_virtual_content", "content", "create_item", "bind_item", "unbind_item"), &SmoothScrollElement::set_virtual_content);
	ClassDB::bind_method(D_METHOD("set_virtual_accessibility", "item_info"), &SmoothScrollElement::set_virtual_accessibility);
	ClassDB::bind_method(D_METHOD("clear_virtual_content"), &SmoothScrollElement::clear_virtual_content);
	ClassDB::bind_method(D_METHOD("set_virtual_grid", "item_count", "columns", "item_size", "separation", "margins"), &SmoothScrollElement::set_virtual_grid, DEFVAL(Vector2()), DEFVAL(Vector4()));
	ClassDB::bind_method(D_METHOD("set_virtual_buffer_rows", "rows"), &SmoothScrollElement::set_virtual_buffer_rows);
	ClassDB::bind_method(D_METHOD("get_virtual_buffer_rows"), &SmoothScrollElement::get_virtual_buffer_rows);
	ClassDB::bind_method(D_METHOD("refresh_virtual_items"), &SmoothScrollElement::refresh_virtual_items);
	ClassDB::bind_method(D_METHOD("refresh_virtual_item", "index"), &SmoothScrollElement::refresh_virtual_item);
	ClassDB::bind_method(D_METHOD("get_virtual_item", "index"), &SmoothScrollElement::get_virtual_item);
	ClassDB::bind_method(D_METHOD("get_virtual_item_index", "item"), &SmoothScrollElement::get_virtual_item_index);
	ClassDB::bind_method(D_METHOD("get_virtual_items"), &SmoothScrollElement::get_virtual_items);
	ClassDB::bind_method(D_METHOD("get_virtual_item_count"), &SmoothScrollElement::get_virtual_item_count);
	ClassDB::bind_method(D_METHOD("get_virtual_pool_size"), &SmoothScrollElement::get_virtual_pool_size);
	ClassDB::bind_method(D_METHOD("scroll_to_virtual_item", "index"), &SmoothScrollElement::scroll_to_virtual_item);
	ClassDB::bind_method(D_METHOD("focus_virtual_item", "index"), &SmoothScrollElement::focus_virtual_item);
}

void SmoothScrollElement::set_virtual_accessibility(const Callable &p_item_info) {
	ERR_FAIL_NULL(virtual_grid);
	virtual_grid->accessibility_info = p_item_info;
	queue_accessibility_update();
}

void SmoothScrollElement::virtual_accessibility_action(const Variant &p_data, int p_index, bool p_focus) {
	if (p_focus) {
		focus_virtual_item(p_index);
	} else {
		scroll_to_virtual_item(p_index);
	}
}

void SmoothScrollElement::update_virtual_accessibility() {
	using namespace AccessibilityServerEnums;
	VirtualGrid &grid = *virtual_grid;
	Control *content = grid.get_content();
	if (!content) {
		return;
	}
	AccessibilityServer *server = AccessibilityServer::get_singleton();
	const RID ae = get_accessibility_element();
	// Keep ordinary scrollbar and other child controls in the accessible tree.
	// The virtual content is represented in logical order, independent of pool order.
	for (int i = 0; i < get_child_count(true); i++) {
		Node *child = get_child(i, true);
		if (child != content) {
			server->update_add_child(ae, child->get_accessibility_element());
		}
	}
	if (grid.accessibility_content.is_null()) {
		grid.accessibility_content = server->create_sub_element(ae, AccessibilityRole::ROLE_LIST);
		grid.accessibility_items.resize(grid.count);
	}
	server->update_set_transform(grid.accessibility_content, content->get_transform());
	server->update_set_bounds(grid.accessibility_content, Rect2(Vector2(), content->get_size()));
	server->update_set_list_item_count(grid.accessibility_content, grid.count);
	HashMap<int, Control *> views;
	for (const VirtualGrid::Item &item : grid.pool) {
		if (item.index >= 0) {
			views.insert(item.index, Object::cast_to<Control>(ObjectDB::get_instance(item.view)));
		}
	}
	for (int index = 0; index < grid.count; index++) {
		RID &item = grid.accessibility_items.write[index];
		const bool created = item.is_null();
		if (created) {
			item = server->create_sub_element(grid.accessibility_content, AccessibilityRole::ROLE_LIST_ITEM);
		}
		server->update_set_bounds(item, grid.item_rect(index));
		server->update_set_list_item_index(item, index);
		server->update_add_action(item, AccessibilityAction::ACTION_SCROLL_INTO_VIEW, callable_mp(this, &SmoothScrollElement::virtual_accessibility_action).bind(index, false));
		server->update_add_action(item, AccessibilityAction::ACTION_FOCUS, callable_mp(this, &SmoothScrollElement::virtual_accessibility_action).bind(index, true));
		server->update_set_name(item, vformat("Item %d", index + 1));
		if (grid.accessibility_info.is_valid()) {
			grid.accessibility_info.call(index, item);
		}
		Control **view = views.getptr(index);
		if (view && *view) {
			// Native controls retain their accessible actions and children. Both the
			// proxy bounds and view transform use the content's coordinate system.
			server->update_add_child(item, (*view)->get_accessibility_element());
		}
	}
}
