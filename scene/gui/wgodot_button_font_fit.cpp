// wgodot-changes::file
#include "button.h"

#include "core/object/class_db.h"

// important note for future: if official godot adds "resize to fit" feature to Button class
// in future, remove these code that are for resize-to-fit.

Ref<TextParagraph> Button::_get_fitted_text(const Size2 &p_available_size) {
	if (!resize_font_to_fit) {
		return text_buf;
	}
	if (!font_fit_dirty && font_fit_size == p_available_size && fitted_text_buf.is_valid() && TS->shaped_text_is_ready(fitted_text_buf->get_rid())) {
		return fitted_text_buf;
	}
	if (fitted_text_buf.is_null()) {
		fitted_text_buf.instantiate();
	}

	// Keep the original paragraph for icon layout and preferred-size calculations.
	// Fitting it in place would make expanding icons depend on the previous frame.
	const float width = MAX(1.0f, p_available_size.width);
	int low = minimum_font_size;
	int high = maximum_font_size;
	int best = low;
	while (low <= high) {
		const int candidate = low + (high - low) / 2;
		_shape(fitted_text_buf, xl_text, candidate);
		fitted_text_buf->set_width(width);
		fitted_text_buf->set_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		// Ellipsis and justification must not make oversized text appear to fit.
		fitted_text_buf->set_text_overrun_behavior(TextServer::OVERRUN_NO_TRIMMING);
		const Size2 text_size = fitted_text_buf->get_size();
		if (text_size.width <= p_available_size.width && text_size.height <= p_available_size.height) {
			best = candidate;
			low = candidate + 1;
		} else {
			high = candidate - 1;
		}
	}

	_shape(fitted_text_buf, xl_text, best);
	fitted_text_buf->set_width(width);
	current_fitted_font_size = best;
	font_fit_size = p_available_size;
	font_fit_dirty = false;
	return fitted_text_buf;
}

void Button::_font_fit_changed() {
	font_fit_dirty = true;
	_queue_update_size_cache();
	update_minimum_size();
	queue_redraw();
}

void Button::set_resize_font_to_fit(bool p_enabled) {
	if (resize_font_to_fit == p_enabled) {
		return;
	}
	resize_font_to_fit = p_enabled;
	if (!resize_font_to_fit) {
		fitted_text_buf.unref();
		current_fitted_font_size = -1;
	}
	_font_fit_changed();
}

bool Button::is_resize_font_to_fit_enabled() const {
	return resize_font_to_fit;
}

void Button::set_minimum_font_size(int p_size) {
	ERR_FAIL_COND_MSG(p_size < 1, "Minimum font size must be positive.");
	if (minimum_font_size == p_size) {
		return;
	}
	minimum_font_size = p_size;
	maximum_font_size = MAX(maximum_font_size, minimum_font_size);
	if (resize_font_to_fit) {
		_font_fit_changed();
	}
}

int Button::get_minimum_font_size() const {
	return minimum_font_size;
}

void Button::set_maximum_font_size(int p_size) {
	ERR_FAIL_COND_MSG(p_size < 1, "Maximum font size must be positive.");
	if (maximum_font_size == p_size) {
		return;
	}
	maximum_font_size = p_size;
	minimum_font_size = MIN(minimum_font_size, maximum_font_size);
	if (resize_font_to_fit) {
		_font_fit_changed();
	}
}

int Button::get_maximum_font_size() const {
	return maximum_font_size;
}

int Button::get_rendered_font_size() const {
	return resize_font_to_fit && current_fitted_font_size > 0 ? current_fitted_font_size : theme_cache.font_size;
}

void Button::_bind_font_fit_methods() {
	ClassDB::bind_method(D_METHOD("set_resize_font_to_fit", "enabled"), &Button::set_resize_font_to_fit);
	ClassDB::bind_method(D_METHOD("is_resize_font_to_fit_enabled"), &Button::is_resize_font_to_fit_enabled);
	ClassDB::bind_method(D_METHOD("set_minimum_font_size", "size"), &Button::set_minimum_font_size);
	ClassDB::bind_method(D_METHOD("get_minimum_font_size"), &Button::get_minimum_font_size);
	ClassDB::bind_method(D_METHOD("set_maximum_font_size", "size"), &Button::set_maximum_font_size);
	ClassDB::bind_method(D_METHOD("get_maximum_font_size"), &Button::get_maximum_font_size);
	ClassDB::bind_method(D_METHOD("get_rendered_font_size"), &Button::get_rendered_font_size);

	ADD_GROUP("Resize Font to Fit", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "resize_font_to_fit", PROPERTY_HINT_GROUP_ENABLE), "set_resize_font_to_fit", "is_resize_font_to_fit_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "minimum_font_size", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), "set_minimum_font_size", "get_minimum_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "maximum_font_size", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), "set_maximum_font_size", "get_maximum_font_size");
}
