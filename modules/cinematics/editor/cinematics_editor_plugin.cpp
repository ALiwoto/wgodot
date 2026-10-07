// wgodot-changes::file
#include "cinematics_editor_plugin.h"

#include "cinematic_preview_resources.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/os/time.h"
#include "editor/docks/editor_dock.h"
#include "editor/editor_interface.h"
#include "editor/themes/editor_scale.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"
#include "servers/audio/audio_server.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

static constexpr int SETTLING_FRAMES = 8;
static constexpr double FRAME_REPEAT_DELAY = 0.35;
static constexpr double FRAME_REPEAT_INTERVAL = 0.08;

CinematicsEditorPanel::CinematicsEditorPanel() {
	set_name("CinematicsEditorPanel");
	HBoxContainer *source_row = memnew(HBoxContainer);
	add_child(source_row);
	Button *load = _button(source_row, TTR("Load / reload open scene"), callable_mp(this, &CinematicsEditorPanel::_load_scene));
	load->set_tooltip_text(TTR("Load a preview of the open scene, including unsaved edits. Reload after changing the scene."));
	scene_label = memnew(Label);
	scene_label->set_h_size_flags(SIZE_EXPAND_FILL);
	scene_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	source_row->add_child(scene_label);
	clock = memnew(Label);
	source_row->add_child(clock);
	_button(source_row, TTR("Copy timestamp"), callable_mp(this, &CinematicsEditorPanel::_copy_timestamp));
	_button(source_row, TTR("Capture frame"), callable_mp(this, &CinematicsEditorPanel::_capture_frame));

	image = memnew(TextureRect);
	image->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	image->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	image->set_v_size_flags(SIZE_EXPAND_FILL);
	image->set_custom_minimum_size(Size2(320, 180) * EDSCALE);
	image->set_focus_mode(FOCUS_ALL);
	image->connect(SNAME("gui_input"), callable_mp(this, &CinematicsEditorPanel::_preview_input));
	add_child(image);
	viewport = memnew(SubViewport);
	int width = GLOBAL_GET("display/window/size/viewport_width");
	int height = GLOBAL_GET("display/window/size/viewport_height");
	int window_width = GLOBAL_GET("display/window/size/window_width_override");
	int window_height = GLOBAL_GET("display/window/size/window_height_override");
	viewport->set_size(Size2i(window_width > 0 ? window_width : width, window_height > 0 ? window_height : height));
	viewport->set_size_2d_override(Size2i(width, height));
	viewport->set_size_2d_override_stretch(true);
	viewport->set_use_own_world_3d(true);
	viewport->set_scaling_3d_scale(1.0);
	viewport->set_use_taa(true);
	viewport->set_disable_input(true);
	viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	add_child(viewport);

	slider = memnew(HSlider);
	slider->set_step(0.001);
	slider->set_editable(false);
	slider->set_custom_minimum_size(Size2(0, 28) * EDSCALE);
	slider->connect(SNAME("value_changed"), callable_mp(this, &CinematicsEditorPanel::_seek));
	slider->connect(SNAME("draw"), callable_mp(this, &CinematicsEditorPanel::_draw_shot_markers));
	add_child(slider);
	HFlowContainer *transport = memnew(HFlowContainer);
	add_child(transport);
	HBoxContainer *playback = memnew(HBoxContainer);
	transport->add_child(playback);
	Button *previous = _icon_button(playback, "CinematicPreviousShot", TTR("Previous shot. Right-click to choose a shot."));
	previous->connect(SNAME("pressed"), callable_mp(this, &CinematicsEditorPanel::_change_shot).bind(-1));
	previous->connect(SNAME("gui_input"), callable_mp(this, &CinematicsEditorPanel::_shot_button_input).bind(previous));
	_frame_button(playback, "CinematicPreviousFrame", TTR("Previous frame. Hold to step repeatedly."), -1);
	play_button = _icon_button(playback, "CinematicPlay", TTR("Play (Space)"));
	play_button->connect(SNAME("pressed"), callable_mp(this, &CinematicsEditorPanel::_toggle_play));
	_frame_button(playback, "CinematicNextFrame", TTR("Next frame. Hold to step repeatedly."), 1);
	Button *next = _icon_button(playback, "CinematicNextShot", TTR("Next shot. Right-click to choose a shot."));
	next->connect(SNAME("pressed"), callable_mp(this, &CinematicsEditorPanel::_change_shot).bind(1));
	next->connect(SNAME("gui_input"), callable_mp(this, &CinematicsEditorPanel::_shot_button_input).bind(next));
	step_timer = memnew(Timer);
	step_timer->set_one_shot(true);
	step_timer->connect(SNAME("timeout"), callable_mp(this, &CinematicsEditorPanel::_repeat_step));
	add_child(step_timer);
	time = _number(transport, TTR("Time (s)"), 0, 1, 0.001, 0);
	time->set_custom_minimum_size(Size2(120, 0) * EDSCALE);
	time->connect(SNAME("value_changed"), callable_mp(this, &CinematicsEditorPanel::_seek));
	speed = _number(transport, TTR("Speed"), 0.1, 4, 0.1, 1);
	speed->set_suffix("x");
	speed->connect(SNAME("value_changed"), callable_mp(this, &CinematicsEditorPanel::_set_audio).unbind(1));
	fps = _number(transport, TTR("Step FPS"), 1, 240, 1, 60);
	preview_fps = _number(transport, TTR("Preview FPS"), 15, 60, 1, 30);
	render_scale = memnew(OptionButton);
	render_scale->add_item(TTR("Native resolution"));
	render_scale->add_item(TTR("Reference quality (150%)"));
	render_scale->connect(SNAME("item_selected"), callable_mp(this, &CinematicsEditorPanel::_render_scale_changed));
	transport->add_child(render_scale);
	loop = memnew(CheckBox);
	loop->set_text(TTR("Loop"));
	transport->add_child(loop);
	audio = memnew(CheckBox);
	audio->set_text(TTR("Audio (1x)"));
	audio->set_tooltip_text(TTR("Audio plays at 1x only; seeking and slow motion are silent."));
	audio->connect(SNAME("toggled"), callable_mp(this, &CinematicsEditorPanel::_set_audio).unbind(1));
	transport->add_child(audio);

	shot_popup = memnew(PopupPanel);
	add_child(shot_popup);
	MarginContainer *margin = memnew(MarginContainer);
	for (const char *side : { "left", "top", "right", "bottom" }) {
		margin->add_theme_constant_override(String("margin_") + side, 12 * EDSCALE);
	}
	shot_popup->add_child(margin);
	VBoxContainer *layout = memnew(VBoxContainer);
	margin->add_child(layout);
	Label *title = memnew(Label);
	title->set_text(TTR("Choose a shot"));
	layout->add_child(title);
	shot_scroll = memnew(ScrollContainer);
	shot_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	shot_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	shot_scroll->set_follow_focus(true);
	layout->add_child(shot_scroll);
	shot_list = memnew(VBoxContainer);
	shot_list->set_h_size_flags(SIZE_EXPAND_FILL);
	shot_list->add_theme_constant_override("separation", 6 * EDSCALE);
	shot_scroll->add_child(shot_list);
	connect(SNAME("visibility_changed"), callable_mp(this, &CinematicsEditorPanel::_visibility_changed));
	set_process(false);
	_set_status();
}

Button *CinematicsEditorPanel::_button(Container *p_row, const String &p_text, const Callable &p_action) {
	Button *button = memnew(Button);
	button->set_text(p_text);
	button->connect(SNAME("pressed"), p_action);
	p_row->add_child(button);
	return button;
}

Button *CinematicsEditorPanel::_icon_button(Container *p_row, const StringName &p_icon, const String &p_tooltip) {
	Button *button = memnew(Button);
	button->set_meta("cinematic_icon", p_icon);
	button->set_expand_icon(true);
	button->add_theme_constant_override("icon_max_width", 20 * EDSCALE);
	button->set_tooltip_text(p_tooltip);
	button->set_custom_minimum_size(Size2(40, 36) * EDSCALE);
	button->set_disabled(true);
	p_row->add_child(button);
	transport_buttons.push_back(button);
	return button;
}

void CinematicsEditorPanel::_update_icons() {
	for (Button *button : transport_buttons) {
		button->set_button_icon(get_theme_icon(button->get_meta("cinematic_icon"), "EditorIcons"));
	}
	if (sequence) {
		_update_clock();
	}
}

void CinematicsEditorPanel::_frame_button(Container *p_row, const StringName &p_icon, const String &p_tooltip, int p_direction) {
	Button *button = _icon_button(p_row, p_icon, p_tooltip);
	button->connect(SNAME("button_down"), callable_mp(this, &CinematicsEditorPanel::_start_step).bind(p_direction));
	button->connect(SNAME("button_up"), callable_mp(this, &CinematicsEditorPanel::_stop_step));
	button->connect(SNAME("mouse_exited"), callable_mp(this, &CinematicsEditorPanel::_stop_step));
	button->connect(SNAME("focus_exited"), callable_mp(this, &CinematicsEditorPanel::_stop_step));
}

SpinBox *CinematicsEditorPanel::_number(Container *p_row, const String &p_title, double p_low, double p_high, double p_step, double p_initial) {
	Label *label = memnew(Label);
	label->set_text(p_title);
	p_row->add_child(label);
	SpinBox *control = memnew(SpinBox);
	control->set_min(p_low);
	control->set_max(p_high);
	control->set_step(p_step);
	control->set_value(p_initial);
	control->set_update_on_text_changed(false);
	p_row->add_child(control);
	return control;
}

void CinematicsEditorPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		get_window()->connect(SNAME("focus_exited"), callable_mp(this, &CinematicsEditorPanel::_stop_step));
		_update_icons();
	} else if (p_what == NOTIFICATION_THEME_CHANGED && play_button) {
		_update_icons();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		_release_preview();
	} else if (p_what == NOTIFICATION_PROCESS && sequence) {
		render_elapsed += get_process_delta_time();
		if (render_elapsed < 1.0 / preview_fps->get_value()) {
			return;
		}
		bool was_playing = sequence->is_playing();
		sequence->advance_playback(render_elapsed * speed->get_value());
		render_elapsed = 0.0;
		if (was_playing && !sequence->is_playing() && loop->is_pressed()) {
			sequence->play();
		}
		viewport->set_update_mode(SubViewport::UPDATE_ONCE);
		_update_clock();
		if (sequence->is_playing()) {
			settling_frames = SETTLING_FRAMES;
		} else {
			settling_frames = MAX(settling_frames - 1, 0);
			if (settling_frames == 0) {
				set_process(false);
				if (capture_pending) {
					RenderingServer::get_singleton()->connect(SNAME("frame_post_draw"), callable_mp(this, &CinematicsEditorPanel::_finish_capture), CONNECT_ONE_SHOT);
				}
			}
		}
	}
}

void CinematicsEditorPanel::set_source(Node *p_scene) {
	_release_preview();
	source_id = p_scene ? p_scene->get_instance_id() : ObjectID();
	_set_status();
}

void CinematicsEditorPanel::_set_status(const String &p_message) {
	Node *source = Object::cast_to<Node>(ObjectDB::get_instance(source_id));
	scene_label->set_text(!p_message.is_empty() ? p_message : source ? source->get_scene_file_path()
																	 : TTR("Open a scene containing a CinematicSequence."));
	scene_label->set_tooltip_text(scene_label->get_text());
}

CinematicSequence *CinematicsEditorPanel::_find_sequence(Node *p_node) const {
	if (CinematicSequence *found = Object::cast_to<CinematicSequence>(p_node)) {
		return found;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (CinematicSequence *found = _find_sequence(p_node->get_child(i))) {
			return found;
		}
	}
	return nullptr;
}

void CinematicsEditorPanel::_load_scene() {
	_release_preview();
	Node *source = Object::cast_to<Node>(ObjectDB::get_instance(source_id));
	if (!source) {
		_set_status(TTR("Open a cinematic scene first."));
		return;
	}
	CinematicSequence *source_sequence = _find_sequence(source);
	if (!source_sequence) {
		_set_status(TTR("This scene has no CinematicSequence. Open a cutscene or its preview scene."));
		return;
	}
	scene_path = source->get_scene_file_path();
	sequence_path = String(source->get_path_to(source_sequence));
	// Packing preserves unsaved edits and resolves exported node arrays inside the copy.
	Ref<PackedScene> packed;
	packed.instantiate();
	Error error = packed->pack(source);
	if (error != OK) {
		_set_status(TTR("Cannot preview this scene: ") + error_names[error]);
		return;
	}
	preview_root = packed->instantiate();
	if (!preview_root) {
		_set_status(TTR("Cannot instantiate the preview scene."));
		return;
	}
	sequence = _find_sequence(preview_root);
	cinematic_isolate_preview_resources(preview_root);
	_isolate_resources(preview_root);
	viewport->add_child(preview_root);
	if (!sequence || !sequence->prepare()) {
		_release_preview();
		_set_status(TTR("Incomplete cinematic: check its shots, cameras and animations."));
		return;
	}
	_populate_shots();
	for (Button *button : transport_buttons) {
		button->set_disabled(false);
	}
	slider->set_max(sequence->get_length());
	slider->set_editable(true);
	time->set_max(sequence->get_length());
	image->set_texture(viewport->get_texture());
	_set_preview_quality(is_visible_in_tree());
	_seek(0.0);
	_set_status();
}

void CinematicsEditorPanel::_isolate_resources(Node *p_node) {
	AudioStreamPlayer *player = Object::cast_to<AudioStreamPlayer>(p_node);
	AudioStreamPlayer2D *player_2d = Object::cast_to<AudioStreamPlayer2D>(p_node);
	AudioStreamPlayer3D *player_3d = Object::cast_to<AudioStreamPlayer3D>(p_node);
	if (player || player_2d || player_3d) {
		StringName original = player ? player->get_bus() : player_2d ? player_2d->get_bus()
																	 : player_3d->get_bus();
		StringName bus = vformat("CinematicPreview_%d_%s", get_instance_id(), original);
		AudioServer *server = AudioServer::get_singleton();
		if (!buses.has(bus)) {
			server->add_bus();
			int index = server->get_bus_count() - 1;
			server->set_bus_name(index, bus);
			server->set_bus_send(index, original);
			server->set_bus_mute(index, true);
			buses.push_back(bus);
		}
		if (player) {
			player->set_bus(bus);
		} else if (player_2d) {
			player_2d->set_bus(bus);
		} else {
			player_3d->set_bus(bus);
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_isolate_resources(p_node->get_child(i));
	}
}

void CinematicsEditorPanel::_release_preview() {
	_stop_step();
	capture_pending = false;
	shot_popup->hide();
	for (Button *button : shot_buttons) {
		memdelete(button);
	}
	shot_buttons.clear();
	for (Button *button : transport_buttons) {
		button->set_disabled(true);
	}
	set_process(false);
	settling_frames = 0;
	if (preview_root) {
		_set_preview_quality(false);
		memdelete(preview_root);
	}
	preview_root = nullptr;
	sequence = nullptr;
	for (const StringName &bus : buses) {
		AudioServer::get_singleton()->remove_bus(AudioServer::get_singleton()->get_bus_index(bus));
	}
	buses.clear();
	viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	slider->set_editable(false);
	slider->set_value_no_signal(0.0);
	slider->queue_redraw();
	time->set_value_no_signal(0.0);
	clock->set_text(String());
	image->set_texture(Ref<Texture2D>());
	if (is_inside_tree()) {
		play_button->set_button_icon(get_theme_icon("CinematicPlay", "EditorIcons"));
	}
	play_button->set_tooltip_text(TTR("Play (Space)"));
}

void CinematicsEditorPanel::_populate_shots() {
	TypedArray<CinematicShot> shots = sequence->get_shots();
	for (int i = 0; i < shots.size(); i++) {
		CinematicShot *shot = Object::cast_to<CinematicShot>(shots[i]);
		Button *button = memnew(Button);
		double start = sequence->get_shot_start(i);
		button->set_text(vformat("%02d  %s   %.3f – %.3f s", i + 1, shot->get_name(), start, start + shot->get_length()));
		button->set_tooltip_text(button->get_text());
		button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		button->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		button->set_clip_text(true);
		button->set_custom_minimum_size(Size2(0, 44) * EDSCALE);
		button->set_toggle_mode(true);
		button->connect(SNAME("pressed"), callable_mp(this, &CinematicsEditorPanel::_select_shot).bind(i));
		shot_list->add_child(button);
		shot_buttons.push_back(button);
	}
}

void CinematicsEditorPanel::_shot_button_input(const Ref<InputEvent> &p_event, Button *p_button) {
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_null() || !mouse->is_pressed() || mouse->get_button_index() != MouseButton::RIGHT || !sequence) {
		return;
	}
	p_button->accept_event();
	_stop_step();
	sequence->pause();
	_update_clock();
	int current = sequence->get_shot_index();
	for (int i = 0; i < shot_buttons.size(); i++) {
		shot_buttons[i]->set_pressed_no_signal(i == current);
	}
	Size2i popup_size = Size2(540, 480) * EDSCALE;
	Point2i position = p_button->get_screen_position() - Vector2(0, popup_size.y);
	shot_popup->popup(Rect2i(position, popup_size));
	shot_buttons[current]->grab_focus();
	shot_scroll->ensure_control_visible(shot_buttons[current]);
}

void CinematicsEditorPanel::_seek(double p_seconds) {
	if (!sequence) {
		return;
	}
	capture_pending = false;
	sequence->show_frame(p_seconds);
	_request_frame();
	_update_clock();
}

void CinematicsEditorPanel::_request_frame() {
	settling_frames = SETTLING_FRAMES;
	render_elapsed = 1.0 / preview_fps->get_value();
	set_process(is_visible_in_tree());
}

void CinematicsEditorPanel::_render_scale_changed(int p_index) {
	viewport->set_scaling_3d_scale(p_index == 1 ? 1.5 : 1.0);
	if (sequence) {
		_request_frame();
	}
}

void CinematicsEditorPanel::_step(int p_direction) {
	if (sequence) {
		_seek(sequence->get_time_seconds() + p_direction / fps->get_value());
	}
}

void CinematicsEditorPanel::_start_step(int p_direction) {
	if (!sequence) {
		return;
	}
	step_direction = p_direction;
	_step(p_direction);
	step_timer->start(FRAME_REPEAT_DELAY);
}

void CinematicsEditorPanel::_stop_step() {
	step_direction = 0;
	step_timer->stop();
}

void CinematicsEditorPanel::_repeat_step() {
	if (!sequence) {
		return;
	}
	double previous = sequence->get_time_seconds();
	_step(step_direction);
	if (sequence->get_time_seconds() != previous) {
		step_timer->start(FRAME_REPEAT_INTERVAL);
	} else {
		_stop_step();
	}
}

void CinematicsEditorPanel::_preview_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_valid()) {
		if (mouse->is_pressed()) {
			image->grab_focus();
		}
		return;
	}
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo()) {
		return;
	}
	switch (key->get_keycode()) {
		case Key::SPACE:
			_toggle_play();
			break;
		case Key::LEFT:
			_step(-1);
			break;
		case Key::RIGHT:
			_step(1);
			break;
		default:
			return;
	}
	image->accept_event();
}

void CinematicsEditorPanel::_draw_shot_markers() {
	if (!sequence) {
		return;
	}
	double margin = slider->get_theme_icon("grabber", "HSlider")->get_width() * 0.5;
	double width = slider->get_size().x - margin * 2.0;
	Color color = get_theme_color("font_color", "Label");
	color.a = 0.5;
	for (int i = 1; i < sequence->get_shots().size(); i++) {
		double x = margin + width * sequence->get_shot_start(i) / sequence->get_length();
		slider->draw_line(Vector2(x, 0), Vector2(x, 7), color);
	}
}

void CinematicsEditorPanel::_toggle_play() {
	_stop_step();
	if (!sequence) {
		return;
	}
	capture_pending = false;
	if (sequence->is_playing()) {
		sequence->pause();
	} else {
		sequence->play();
	}
	_request_frame();
	_update_clock();
}

void CinematicsEditorPanel::_select_shot(int p_index) {
	shot_popup->hide();
	if (sequence) {
		_seek(sequence->get_shot_start(p_index));
	}
}

void CinematicsEditorPanel::_change_shot(int p_direction) {
	if (sequence) {
		_select_shot(CLAMP(sequence->get_shot_index() + p_direction, 0, sequence->get_shots().size() - 1));
	}
}

void CinematicsEditorPanel::_update_clock() {
	slider->set_value_no_signal(sequence->get_time_seconds());
	time->set_value_no_signal(sequence->get_time_seconds());
	play_button->set_button_icon(get_theme_icon(sequence->is_playing() ? "CinematicPause" : "CinematicPlay", "EditorIcons"));
	play_button->set_tooltip_text(sequence->is_playing() ? TTR("Pause (Space)") : TTR("Play (Space)"));
	clock->set_text(vformat("%.3f / %.3f s", sequence->get_time_seconds(), sequence->get_length()));
	clock->set_tooltip_text(_timestamp());
	_set_audio();
}

void CinematicsEditorPanel::_set_audio() {
	bool muted = !sequence || !sequence->is_playing() || !is_visible_in_tree() || !audio->is_pressed() || !Math::is_equal_approx(speed->get_value(), 1.0);
	for (const StringName &bus : buses) {
		AudioServer::get_singleton()->set_bus_mute(AudioServer::get_singleton()->get_bus_index(bus), muted);
	}
}

void CinematicsEditorPanel::_visibility_changed() {
	if (!is_visible_in_tree()) {
		_stop_step();
		shot_popup->hide();
		capture_pending = false;
	}
	if (!sequence) {
		return;
	}
	_set_preview_quality(is_visible_in_tree());
	if (!is_visible_in_tree()) {
		sequence->pause();
		set_process(false);
		viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
		_update_clock();
	} else {
		_request_frame();
	}
}

void CinematicsEditorPanel::_set_preview_quality(bool p_enabled) {
	RSE::DOFBokehShape shape = RSE::DOF_BOKEH_CIRCLE;
	RSE::DOFBlurQuality quality = RSE::DOF_BLUR_QUALITY_HIGH;
	bool jitter = true;
	if (!p_enabled) {
		shape = (RSE::DOFBokehShape)(int)GLOBAL_GET("rendering/camera/depth_of_field/depth_of_field_bokeh_shape");
		quality = (RSE::DOFBlurQuality)(int)GLOBAL_GET("rendering/camera/depth_of_field/depth_of_field_bokeh_quality");
		jitter = GLOBAL_GET("rendering/camera/depth_of_field/depth_of_field_use_jitter");
	}
	RenderingServer::get_singleton()->camera_attributes_set_dof_blur_bokeh_shape(shape);
	RenderingServer::get_singleton()->camera_attributes_set_dof_blur_quality(quality, jitter);
}

String CinematicsEditorPanel::_timestamp() const {
	int index = sequence->get_shot_index();
	CinematicShot *shot = Object::cast_to<CinematicShot>(sequence->get_shots()[index]);
	return vformat("%s | %s | %.6f s | shot %02d (%s), +%.6f s | %.2fx", scene_path, sequence_path, sequence->get_time_seconds(), index + 1, shot->get_name(), sequence->get_time_seconds() - sequence->get_shot_start(index), speed->get_value());
}

void CinematicsEditorPanel::_copy_timestamp() {
	if (sequence) {
		DisplayServer::get_singleton()->clipboard_set(_timestamp());
		_set_status(TTR("Copied scene, timestamp, shot and speed."));
	}
}

void CinematicsEditorPanel::_capture_frame() {
	if (!sequence) {
		return;
	}
	_stop_step();
	sequence->pause();
	_request_frame();
	_update_clock();
	capture_pending = true;
}

void CinematicsEditorPanel::_finish_capture() {
	if (!capture_pending || !sequence || !is_visible_in_tree()) {
		return;
	}
	capture_pending = false;
	String directory = ProjectSettings::get_singleton()->globalize_path("res://.local-tmp/screenshots");
	Error error = DirAccess::make_dir_recursive_absolute(directory);
	if (error != OK) {
		_set_status(TTR("Cannot create capture directory: ") + error_names[error]);
		return;
	}
	String stamp = Time::get_singleton()->get_datetime_string_from_system().replace(":", "-");
	String path = directory.path_join(vformat("%s-%.3fs-%s", sequence->get_name(), sequence->get_time_seconds(), stamp));
	error = viewport->get_texture()->get_image()->save_png(path + ".png");
	if (error != OK) {
		_set_status(TTR("Cannot save capture: ") + error_names[error]);
		return;
	}
	Ref<FileAccess> report = FileAccess::open(path + ".txt", FileAccess::WRITE, &error);
	if (report.is_null()) {
		_set_status(TTR("Saved image; could not write its timestamp: ") + error_names[error]);
		return;
	}
	report->store_line(_timestamp());
	_set_status(TTR("Saved ") + path + ".png");
}

CinematicsEditorPlugin::CinematicsEditorPlugin() {
	dock = memnew(EditorDock);
	dock->set_title(TTR("Cinematics"));
	dock->set_layout_key("Cinematics");
	dock->set_icon_name("AnimationPlayer");
	dock->set_default_slot(EditorDock::DOCK_SLOT_MAIN_SCREEN);
	dock->set_available_layouts(EditorDock::DOCK_LAYOUT_MAIN_SCREEN | EditorDock::DOCK_LAYOUT_FLOATING);
	panel = memnew(CinematicsEditorPanel);
	dock->add_child(panel);
	connect(SNAME("scene_changed"), callable_mp(this, &CinematicsEditorPlugin::_scene_changed));
}

void CinematicsEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_dock(dock);
		_scene_changed(EditorInterface::get_singleton()->get_edited_scene_root());
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		remove_dock(dock);
		memdelete(dock);
		dock = nullptr;
		panel = nullptr;
	}
}

void CinematicsEditorPlugin::_scene_changed(Node *p_scene) {
	panel->set_source(p_scene);
}
